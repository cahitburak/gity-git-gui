// The three versions git keeps of a conflicted file, read for the conflict
// editor.
//
// A conflict leaves up to three entries for a path in the index: stage 1, the
// common ancestor; stage 2, ours; stage 3, theirs. Any of them can be
// missing — no ancestor when both sides added the file, no ours or theirs
// when one side deleted it. Each is read through the checkout filters (line
// endings, ident), so the text is what the file holds on disk and what the
// editor saves goes back the same way.
#pragma once

#include "Merge3.h"
#include "Repository.h"

#include <cstdint>
#include <string>

namespace gity::git {

struct ConflictSide {
    bool present = false;
    /// The git file mode of this version: a regular or executable file, a
    /// symlink (0120000) or a submodule (0160000). 0 when it is not present.
    std::uint32_t mode = 0;
    /// Read only when the versions are merged line by line; empty for a
    /// conflict that can only be taken whole.
    std::string text;
};

/// A commit a side of the conflict comes from, as the editor names it.
struct ConflictCommit {
    /// The branch, when HEAD is on one; otherwise empty.
    std::string branch;
    std::string shortId;
    std::string summary;
};

struct ConflictFile {
    std::string path;
    ConflictSide base;
    ConflictSide ours;
    ConflictSide theirs;
    /// Only a whole side can be taken: a version holds binary content, or is
    /// not a file at all but a symlink or a submodule (see each side's mode).
    bool binary = false;

    /// HEAD — what "ours" is — and the commit being merged, picked, reverted
    /// or replayed — "theirs". Mid-rebase that is turned round: HEAD is the
    /// branch being rebased onto, and theirs is your own commit.
    ConflictCommit head;
    ConflictCommit incoming;

    /// The regions, when every version is text and both sides have the file.
    Merge3 merge;

    /// Whether both sides still have the file: the editor's case. When one
    /// side deleted it the choice is keep or delete.
    [[nodiscard]] bool bothSides() const noexcept { return ours.present && theirs.present; }
};

/// The versions of `path`, relative to the top of the working copy. Throws
/// when the path is not conflicted.
[[nodiscard]] ConflictFile loadConflict(git_repository* repo, const std::string& path);

} // namespace gity::git
