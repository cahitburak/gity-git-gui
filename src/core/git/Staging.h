// M2 — moving changes into and out of the index.
//
// Whole-file staging is a libgit2 index call. Hunk and line staging is not:
// libgit2 can apply a patch to the index but has no notion of "these lines
// only", so the selection is turned into a synthetic patch first. That
// synthesis is pure string work, which is why it is exposed separately and
// tested on its own — it is the part that can be subtly wrong.
//
// ADR-003 still holds: the *commit* goes through the git binary so hooks,
// signing and filters behave. Only index manipulation happens in-process.
#pragma once

#include "FileDiff.h"
#include "Repository.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace gity::git {

/// Which side of the index a working-copy diff describes.
enum class DiffSide {
    Staged,   ///< HEAD → index. What a commit would contain.
    Unstaged, ///< index → worktree. What staging would add.
    /// HEAD → worktree. Every change to the file, staged or not.
    ///
    /// Its new-side line numbers are worktree lines, exactly as the Unstaged
    /// side's are, which is what makes the two comparable — and comparing them
    /// is the only way to say how much of a region is already staged.
    Combined,
};

/// Diffs one path in the working copy rather than in history.
[[nodiscard]] FileDiff loadWorkingDiff(git_repository* repo, const std::string& path,
                                       DiffSide side, const FileDiffOptions& options = {});

// --- whole file ----------------------------------------------------------

/// Stages several paths as one index write. Staging a selection one file at
/// a time would leave the index briefly split, and a filesystem watcher can
/// observe that.
void stageFiles(git_repository* repo, const std::vector<std::string>& paths);
void unstageFiles(git_repository* repo, const std::vector<std::string>& paths);

// --- hunks and lines -----------------------------------------------------

/// Line indices into FileDiff::lines. Context lines may be included or not;
/// they are always emitted regardless.
using LineSelection = std::unordered_set<std::uint32_t>;

/// Every changed line in one hunk, as a selection.
[[nodiscard]] LineSelection linesOfHunk(const FileDiff& diff, std::size_t hunkIndex);

/// Builds a unified diff containing only the selected changes.
///
/// Unselected additions are dropped — they are not in the result. Unselected
/// deletions become context — the line stays. Hunk headers are recomputed
/// from what survives, and a hunk with nothing selected is omitted entirely.
/// This is the same transformation `git add -p` performs.
///
/// `reverse` swaps the direction, which is how unstaging works: the same
/// selection applied backwards.
[[nodiscard]] std::string buildPartialPatch(const FileDiff& diff,
                                            const LineSelection& selected, bool reverse);

/// Applies a patch to the index, leaving the worktree untouched.
///
/// Exposed because the UI builds the patch from the diff it is already showing
/// and sends that, rather than sending a line selection for the worker to
/// re-resolve. If the worktree moved in between, applying a stale patch fails
/// cleanly; re-resolving stale indices would silently stage the wrong lines.
void applyPatchToIndex(git_repository* repo, const std::string& patchText);

/// Applies a patch to the worktree only — what discarding an unstaged hunk
/// means. Separate from applyPatchToIndex because the two differ only in a
/// location flag and confusing them silently rewrites the user's files.
///
/// The worktree and not both: the patch is the reverse of an index-to-worktree
/// hunk, which the index never held, so applying it there as well can only
/// fail.
void applyPatchToWorktree(git_repository* repo, const std::string& patchText);

} // namespace gity::git
