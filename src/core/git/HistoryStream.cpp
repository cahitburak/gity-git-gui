#include "HistoryStream.h"

#include "../graph/LaneAssigner.h"

#include <algorithm>
#include <cctype>

#include <chrono>

namespace gity::git {

bool commitMatches(std::string_view summary, std::string_view author, std::string_view idHex,
                   std::string_view filter) {
    if (filter.empty()) {
        return true;
    }
    const auto containsFold = [](std::string_view haystack, std::string_view needle) {
        if (needle.size() > haystack.size()) {
            return false;
        }
        const auto fold = [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        };
        return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                           [&](char a, char b) { return fold(static_cast<unsigned char>(a)) ==
                                                        fold(static_cast<unsigned char>(b)); }) !=
               haystack.end();
    };
    // Id matched as a prefix rather than anywhere: a hex fragment appearing in
    // the middle of an unrelated hash is a coincidence, not a match.
    return containsFold(summary, filter) || containsFold(author, filter) ||
           (idHex.size() >= filter.size() &&
            containsFold(idHex.substr(0, filter.size()), filter));
}

bool CommitFilter::matches(std::string_view summary, std::string_view authorName,
                           std::string_view idHex, std::int64_t when) const {
    if (since != 0 && when < since) {
        return false;
    }
    if (until != 0 && when > until) {
        return false;
    }
    if (!author.empty() && !commitMatches({}, authorName, {}, author)) {
        return false;
    }
    return text.empty() || commitMatches(summary, authorName, idHex, text);
}

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

/// push_glob reports ENOTFOUND for a namespace with no refs; that is not an
/// error, it just means the repository has no remotes or no branches.
void pushGlobIfPresent(git_revwalk* walk, const char* glob) {
    const int rc = git_revwalk_push_glob(walk, glob);
    if (rc < 0 && rc != GIT_ENOTFOUND) {
        throwLastError(rc, "push refs onto the revwalk");
    }
}

std::string_view summaryOf(git_commit* commit) {
    const char* summary = git_commit_summary(commit);
    return (summary != nullptr) ? std::string_view(summary) : std::string_view();
}

std::string_view authorOf(git_commit* commit) {
    const git_signature* author = git_commit_author(commit);
    if (author == nullptr || author->name == nullptr) {
        return {};
    }
    return {author->name};
}

} // namespace

StreamStats streamHistory(git_repository* repo, const StreamOptions& options,
                          const ChunkSink& sink) {
    const auto started = Clock::now();
    StreamStats stats;

    RevwalkHandle walk;
    check(git_revwalk_new(walk.receive(), repo), "create a revwalk");
    git_revwalk_sorting(walk.get(), GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
    pushGlobIfPresent(walk.get(), "refs/heads/*");
    pushGlobIfPresent(walk.get(), "refs/remotes/*");
    // HEAD as well, for when it is on no branch: a rebase in progress, a
    // checked-out tag, commits made while detached. None of those are
    // reachable from a branch, and leaving HEAD out made the commit you are
    // standing on missing from the graph. An unborn HEAD has nothing to push.
    if (const int head = git_revwalk_push_head(walk.get());
        head < 0 && head != GIT_ENOTFOUND && head != GIT_EUNBORNBRANCH) {
        throwLastError(head, "push HEAD onto the revwalk");
    }

    // Slot state persists across chunks; that is what makes streaming and
    // whole-table assignment produce identical graphs.
    graph::LaneAssigner assigner;

    model::HistoryChunk chunk;
    chunk.commits.reserve(options.batchSize);
    chunk.graph.rows.reserve(options.batchSize);
    chunk.graph.edges.reserve(options.batchSize * 3);

    std::vector<graph::CommitKey> parentKeys;
    std::size_t globalRow = 0;
    bool firstChunkEmitted = false;

    const auto emit = [&](bool final) -> bool {
        if (chunk.graph.rows.empty() && !final) {
            return true;
        }
        if (!firstChunkEmitted) {
            stats.firstChunkMs = msSince(started);
            firstChunkEmitted = true;
        }
        const std::size_t emittedRows = chunk.graph.rows.size();
        model::HistoryChunk out = std::move(chunk);

        chunk = model::HistoryChunk{};
        chunk.firstRow = globalRow;
        chunk.commits.reserve(options.batchSize);
        chunk.graph.rows.reserve(options.batchSize);
        chunk.graph.edges.reserve(options.batchSize * 3);

        if (emittedRows == 0) {
            return true;
        }
        return sink(std::move(out));
    };

    const bool filtering = options.filter.active();
    char oidHex[GIT_OID_SHA1_HEXSIZE + 1] = {};

    git_oid oid;
    int rc = 0;
    std::size_t visited = 0;
    while ((rc = git_revwalk_next(&oid, walk.get())) == 0) {
        if ((++visited & 0x3FF) == 0 && options.cancel != nullptr &&
            options.cancel->load(std::memory_order_relaxed)) {
            stats.cancelled = true;
            stats.totalMs = msSince(started);
            return stats;
        }
        CommitHandle commit;
        check(git_commit_lookup(commit.receive(), repo, &oid), "look up a commit");

        const auto summary = summaryOf(commit.get());
        const auto author = authorOf(commit.get());
        if (filtering) {
            git_oid_tostr(oidHex, sizeof(oidHex), &oid);
            if (!options.filter.matches(summary, author, oidHex,
                                        static_cast<std::int64_t>(git_commit_time(commit.get())))) {
                continue;
            }
        }

        parentKeys.clear();
        const unsigned int parentCount = git_commit_parentcount(commit.get());
        for (unsigned int p = 0; p < parentCount; ++p) {
            parentKeys.push_back(keyFromOid(*git_commit_parent_id(commit.get(), p)));
        }

        if (filtering) {
            // One lane, no edges. The commits between two matches were skipped,
            // so any line drawn between them would join commits that are not
            // adjacent in the real history.
            graph::GraphRow row;
            row.lane = 0;
            row.laneCount = 1;
            chunk.graph.rows.push_back(row);
        } else {
            assigner.append(keyFromOid(oid), parentKeys, chunk.graph);
        }
        chunk.commits.append(oid, summary, author,
                             static_cast<std::int64_t>(git_commit_time(commit.get())));

        ++globalRow;
        ++stats.commits;

        if (chunk.graph.rows.size() >= options.batchSize) {
            if (!emit(false)) {
                stats.cancelled = true;
                stats.totalMs = msSince(started);
                return stats;
            }
        }
    }

    if (rc != GIT_ITEROVER && rc < 0) {
        throwLastError(rc, "walk the commit history");
    }

    if (!emit(true)) {
        stats.cancelled = true;
    }

    stats.totalMs = msSince(started);
    if (!firstChunkEmitted) {
        stats.firstChunkMs = stats.totalMs;
    }
    return stats;
}

} // namespace gity::git
