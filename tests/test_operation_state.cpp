#include "core/git/OperationState.h"

#include <gtest/gtest.h>

using gity::git::classifyOperation;
using gity::git::Operation;
using gity::git::OperationMarkers;

TEST(ClassifyOperation, NothingRunning) {
    EXPECT_EQ(classifyOperation({}), Operation::None);
}

TEST(ClassifyOperation, EachSequencerOperationOnItsOwn) {
    OperationMarkers merge;
    merge.mergeHead = true;
    EXPECT_EQ(classifyOperation(merge), Operation::Merge);

    OperationMarkers pick;
    pick.cherryPickHead = true;
    EXPECT_EQ(classifyOperation(pick), Operation::CherryPick);

    OperationMarkers revert;
    revert.revertHead = true;
    EXPECT_EQ(classifyOperation(revert), Operation::Revert);
}

TEST(ClassifyOperation, ARebaseWinsOverASequencerMarkerIfBothEverAppear) {
    // Checked against git 2.55: a rebase stopped by a conflict, a `rebase -i`
    // stopped by a conflict, and a `rebase -i` stopped at an `edit` all leave
    // rebase-merge/ *alone* — CHERRY_PICK_HEAD was not present in any of them.
    // So this ordering is defensive rather than a case observed today.
    //
    // It is kept because the cost is asymmetric. If a git version ever does
    // leave both (older ones did, and rebase really is implemented by the same
    // sequencer), reading it as a cherry-pick offers `cherry-pick --abort`,
    // which discards the step while leaving the rebase running — a worse state
    // than the one the user was already in. Reading a lone cherry-pick as a
    // rebase cannot happen, because rebase-merge/ would have to exist.
    OperationMarkers markers;
    markers.rebaseMergeDir = true;
    markers.cherryPickHead = true;
    EXPECT_EQ(classifyOperation(markers), Operation::RebaseInteractive);
    EXPECT_EQ(gity::git::operationCommand(classifyOperation(markers)), "rebase");
}

TEST(ClassifyOperation, TheOlderRebaseBackendIsStillARebase) {
    OperationMarkers markers;
    markers.rebaseApplyDir = true;
    EXPECT_EQ(classifyOperation(markers), Operation::RebaseApply);
    EXPECT_EQ(gity::git::operationCommand(classifyOperation(markers)), "rebase");
}

TEST(ClassifyOperation, ABisectUnderneathAMergeReportsTheMerge) {
    // A bisect can run underneath anything and is the least urgent thing to
    // say; the merge is what is blocking the next commit.
    OperationMarkers markers;
    markers.bisectLog = true;
    markers.mergeHead = true;
    EXPECT_EQ(classifyOperation(markers), Operation::Merge);
}

TEST(ClassifyOperation, ABisectAloneIsWorthReporting) {
    OperationMarkers markers;
    markers.bisectLog = true;
    EXPECT_EQ(classifyOperation(markers), Operation::Bisect);
}

TEST(OperationNoun, ReadsAsPartOfASentence) {
    EXPECT_EQ(gity::git::operationNoun(Operation::Merge), "a merge");
    EXPECT_EQ(gity::git::operationNoun(Operation::CherryPick), "a cherry-pick");
    EXPECT_TRUE(gity::git::operationNoun(Operation::None).empty());
}

TEST(OperationCommand, BisectAndNoneOwnNoAbortFlag) {
    // Both would otherwise produce `git  --abort`.
    EXPECT_TRUE(gity::git::operationCommand(Operation::Bisect).empty());
    EXPECT_TRUE(gity::git::operationCommand(Operation::None).empty());
}
