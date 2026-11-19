// ADR-003 read path + ADR-005 streaming.
//
// M0 measured a cold 1M-commit walk at roughly four seconds, against a budget
// of one second to first painted rows. The resolution is not a faster walk —
// the walk itself is the floor — it is to stop waiting for it. This emits
// chunks as the revwalk produces them, with lane assignment running
// incrementally so each chunk arrives paintable.
#pragma once

#include "../model/HistoryChunk.h"
#include "Repository.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace gity::git {

/// Identity used by lane assignment. The leading 8 bytes of the OID: unique
/// enough that a collision inside the ~20 live slots is not a practical
/// concern, and a mismatch would misdraw one edge rather than corrupt state.
inline graph::CommitKey keyFromOid(const git_oid& oid) noexcept {
    graph::CommitKey key = 0;
    std::memcpy(&key, oid.id, sizeof(key));
    return key;
}

/// What the history is narrowed to. Every part is optional; all that are set
/// must match.
struct CommitFilter {
    /// Summary, author or id containing this, case-insensitively.
    std::string text;
    /// Author name containing this, case-insensitively — a contributor chosen
    /// from the list, or part of a name typed.
    std::string author;
    /// Commit time bounds, seconds since the epoch, inclusive. Zero is open.
    std::int64_t since = 0;
    std::int64_t until = 0;
    /// A file or folder, relative to the top of the working copy with `/`
    /// between parts: only commits that changed it. Checked by the walk, not
    /// by matches(), since it needs the commit's trees.
    std::string path;
    /// For a file: carry on past a rename under the file's older name, as
    /// `git log --follow` does. Ignored for a folder.
    bool followRenames = true;

    [[nodiscard]] bool active() const noexcept {
        return !text.empty() || !author.empty() || since != 0 || until != 0 || !path.empty();
    }

    [[nodiscard]] bool matches(std::string_view summary, std::string_view authorName,
                               std::string_view idHex, std::int64_t when) const;
    [[nodiscard]] bool operator==(const CommitFilter&) const = default;
};

struct StreamOptions {
    /// Rows per chunk. Small enough that the first paint is immediate, large
    /// enough that queued-signal overhead stays irrelevant.
    std::size_t batchSize = 2048;

    /// Keep only commits the filter matches. Inactive walks everything.
    ///
    /// A filtered walk produces a **list, not a graph**: the commits between
    /// two matches are missing, so the lanes and edges joining them describe a
    /// history that does not exist. Rows therefore come back on a single lane
    /// with no edges when this is set, and the UI says it is showing matches
    /// rather than history. Drawing a plausible graph over a subset would be
    /// worse than drawing none.
    CommitFilter filter;

    /// Checked as the walk goes, not only when a chunk is handed over. A
    /// filter that matches rarely fills a chunk slowly, and without this a
    /// superseded filter walked the whole history before noticing.
    const std::atomic<bool>* cancel = nullptr;
};

/// What one commit did to a file or folder, and what it was called before.
struct PathChange {
    /// Whether the commit changed it, as `git log -- path` decides: a commit
    /// is kept unless what `path` names is the same in it as in one of its
    /// parents, mode included. A merge that took one side's version unchanged is therefore
    /// left out, as git leaves it out. A rename is a change.
    bool touched = false;
    /// Per parent, the name the file had there when this commit renamed it to
    /// `path`; empty where the name did not change. Only filled when
    /// following renames, and only for a file — git follows no folder either.
    std::vector<std::string> renamedFrom;
    /// Per parent, whether `path` names anything there. Only filled when
    /// following renames: it is how the walk tells a name a parent really has
    /// from one passed down by a child that never had the file.
    std::vector<bool> inParent;
};

/// How `commit` changed `path`. With `followRenames`, a file that is new in
/// the commit is looked for among the files the commit deleted, as `git log
/// --follow` does, so the walk can carry on under its older name.
///
/// Unlike git, the sides of a merge that did not reach its result are still
/// walked, so a change a merge threw away can be listed.
[[nodiscard]] PathChange pathChange(git_repository* repo, git_commit* commit,
                                    const std::string& path, bool followRenames);

/// Whether `commit` changed `path`, not following renames.
[[nodiscard]] bool commitTouchesPath(git_repository* repo, git_commit* commit,
                                     const std::string& path);

/// True when `filter` matches any of the fields a person would search.
[[nodiscard]] bool commitMatches(std::string_view summary, std::string_view author,
                                 std::string_view idHex, std::string_view filter);

struct StreamStats {
    std::size_t commits = 0;
    double firstChunkMs = 0.0; ///< Time to the first paintable rows.
    double totalMs = 0.0;
    bool cancelled = false;
};

/// Invoked once per chunk, on the calling thread. Return false to cancel the
/// walk — used for both explicit cancellation and superseded requests.
using ChunkSink = std::function<bool(model::HistoryChunk&&)>;

StreamStats streamHistory(git_repository* repo, const StreamOptions& options,
                          const ChunkSink& sink);

} // namespace gity::git
