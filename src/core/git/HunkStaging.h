// "· 2 of 4 lines staged" — SPEC.md §2, the hunk header.
//
// The count cannot be read off the diff being displayed. The staging pane
// shows index → worktree, and every changed line in it is by definition *not*
// staged; the staged side (HEAD → index) is numbered against the index, so its
// line numbers cannot be lined up with anything the user is looking at.
//
// What can be compared is HEAD → worktree against index → worktree: both are
// numbered against the worktree file. Every change to a region appears in the
// first; the ones still to stage appear in the second; the difference is what
// is already staged.
//
// Pure, and tested against diffs built by hand, because the arithmetic is the
// part that goes quietly wrong.
#pragma once

#include "core/git/FileDiff.h"

#include <cstddef>
#include <vector>

namespace gity::git {

struct HunkStaging {
    std::size_t staged = 0;
    std::size_t total = 0;

    /// Worth showing only when part of the region is staged and part is not.
    /// "0 of 4" on a file nobody has touched the index for is noise, and
    /// "4 of 4" cannot happen in a pane that shows what is left to stage.
    [[nodiscard]] bool partial() const noexcept { return staged > 0 && staged < total; }
};

/// One entry per hunk of `unstaged`, in order.
///
/// `combined` is the HEAD → worktree diff of the same file. When it is empty —
/// nothing loaded it, or the file is untracked — every count comes back with
/// `staged == 0`, which reads as "nothing here is staged" and is true.
[[nodiscard]] std::vector<HunkStaging> hunkStagingCounts(const FileDiff& unstaged,
                                                         const FileDiff& combined);

} // namespace gity::git
