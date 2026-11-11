// M2 — reading the working copy's diffs, and turning a selection of lines into
// a patch.
//
// Nothing here writes. Staging, unstaging and discarding go through the git
// binary (session/IndexWrites), as ADR-003 requires: libgit2's index calls run
// none of the user's external clean filters, so a file under LFS went into
// the index whole and one under git-crypt in plaintext.
//
// Hunk and line staging is a patch built from the selection and handed to
// `git apply`. Building it is pure string work, which is why it is exposed
// separately and tested on its own — it is the part that can be subtly wrong.
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
[[nodiscard]] FileDiff loadWorkingDiff(git_repository* repo, const std::string& path, DiffSide side,
                                       const FileDiffOptions& options = {});

// --- hunks and lines -----------------------------------------------------

/// Line indices into FileDiff::lines. Context lines may be included or not;
/// they are always emitted regardless.
using LineSelection = std::unordered_set<std::uint32_t>;

/// Every changed line in one hunk, as a selection.
[[nodiscard]] LineSelection linesOfHunk(const FileDiff& diff, std::size_t hunkIndex);

/// Builds a unified diff containing only the selected changes.
///
/// Forward (staging) the patch applies to the diff's old side: unselected
/// additions are dropped, and unselected deletions become context. Reversed
/// (unstaging, discarding) it applies to the new side and undoes the selected
/// changes: unselected additions become context, and unselected deletions are
/// dropped. Hunk headers are recomputed from what survives, and a hunk with
/// nothing selected is omitted entirely — the same transformation `git add -p`
/// performs. Lines keep their CR LF endings, and a patch against a side that
/// does not exist yet says it creates the file.
[[nodiscard]] std::string buildPartialPatch(const FileDiff& diff, const LineSelection& selected,
                                            bool reverse);

} // namespace gity::git
