// What the repository is in the middle of.
//
// Merge, cherry-pick, revert and rebase can all stop halfway and leave the
// repository in a state where the next thing to do is not "commit" but
// "finish or abandon what is already underway". git records that in files
// under .git, and a client that does not read them will cheerfully offer to
// commit a half-finished merge as though it were ordinary work.
//
// Precedence is decided here rather than by whichever check happens to run
// first, because reporting the wrong operation means offering the wrong
// `--abort`, and that destroys work. See the note in classifyOperation.
#pragma once

#include <string>
#include <string_view>

namespace gity::git {

enum class Operation {
    None,
    Merge,
    CherryPick,
    Revert,
    /// `git rebase -i`, and also any rebase that stopped — modern git uses the
    /// merge backend for both.
    RebaseInteractive,
    /// The older `git am`-based backend, still produced by `git am` itself.
    RebaseApply,
    Bisect,
};

/// The marker files present under .git. Separated from the filesystem so the
/// precedence rules can be tested without building a repository for each case.
struct OperationMarkers {
    bool mergeHead = false;       ///< MERGE_HEAD
    bool cherryPickHead = false;  ///< CHERRY_PICK_HEAD
    bool revertHead = false;      ///< REVERT_HEAD
    bool rebaseMergeDir = false;  ///< rebase-merge/
    bool rebaseApplyDir = false;  ///< rebase-apply/
    bool bisectLog = false;       ///< BISECT_LOG
};

[[nodiscard]] Operation classifyOperation(const OperationMarkers& markers);

/// Reads the markers from `gitDir` and classifies them.
[[nodiscard]] Operation detectOperation(const std::string& gitDir);

/// What to call the operation in a sentence, lowercase: "a merge", "a
/// cherry-pick". Empty for None.
[[nodiscard]] std::string_view operationNoun(Operation operation);

/// The git subcommand that owns `--abort` and `--continue` for this operation,
/// e.g. "merge", "cherry-pick". Empty for None and Bisect, neither of which
/// takes those flags in the same way.
[[nodiscard]] std::string_view operationCommand(Operation operation);

} // namespace gity::git
