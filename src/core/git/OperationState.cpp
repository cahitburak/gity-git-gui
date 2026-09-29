#include "OperationState.h"

#include <filesystem>

namespace gity::git {

Operation classifyOperation(const OperationMarkers& markers) {
    // Rebase first, defensively. git 2.55 leaves rebase-merge/ on its own —
    // measured for a conflicted rebase, a conflicted `rebase -i` and a
    // `rebase -i` stopped at an `edit` — so this precedence does not change
    // any behaviour observed today.
    //
    // It is here because the cost is asymmetric. Rebase is implemented by the
    // same sequencer as cherry-pick and older git versions did leave both
    // markers; if that ever recurs, reading it as a cherry-pick would offer
    // `cherry-pick --abort`, which discards the step and leaves the rebase
    // running. The reverse mistake is impossible: a lone cherry-pick has no
    // rebase-merge/ to be confused by.
    if (markers.rebaseMergeDir) {
        return Operation::RebaseInteractive;
    }
    if (markers.rebaseApplyDir) {
        return Operation::RebaseApply;
    }

    // Then the sequencer operations. These are mutually exclusive in practice;
    // the order below is only a tie-break.
    if (markers.cherryPickHead) {
        return Operation::CherryPick;
    }
    if (markers.revertHead) {
        return Operation::Revert;
    }
    if (markers.mergeHead) {
        return Operation::Merge;
    }

    // Last: a bisect can be running underneath any of the above, and while it
    // is the least urgent thing to report, it is worth reporting when it is
    // the only thing happening.
    if (markers.bisectLog) {
        return Operation::Bisect;
    }
    return Operation::None;
}

Operation detectOperation(const std::string& gitDir) {
    namespace fs = std::filesystem;
    const fs::path root(gitDir);
    std::error_code error;

    OperationMarkers markers;
    markers.mergeHead = fs::exists(root / "MERGE_HEAD", error);
    markers.cherryPickHead = fs::exists(root / "CHERRY_PICK_HEAD", error);
    markers.revertHead = fs::exists(root / "REVERT_HEAD", error);
    markers.rebaseMergeDir = fs::is_directory(root / "rebase-merge", error);
    markers.rebaseApplyDir = fs::is_directory(root / "rebase-apply", error);
    markers.bisectLog = fs::exists(root / "BISECT_LOG", error);
    return classifyOperation(markers);
}

std::string_view operationNoun(Operation operation) {
    switch (operation) {
    case Operation::Merge:
        return "a merge";
    case Operation::CherryPick:
        return "a cherry-pick";
    case Operation::Revert:
        return "a revert";
    case Operation::RebaseInteractive:
    case Operation::RebaseApply:
        return "a rebase";
    case Operation::Bisect:
        return "a bisect";
    case Operation::None:
        break;
    }
    return {};
}

std::string_view operationCommand(Operation operation) {
    switch (operation) {
    case Operation::Merge:
        return "merge";
    case Operation::CherryPick:
        return "cherry-pick";
    case Operation::Revert:
        return "revert";
    case Operation::RebaseInteractive:
    case Operation::RebaseApply:
        return "rebase";
    case Operation::Bisect:
    case Operation::None:
        break;
    }
    return {};
}

} // namespace gity::git
