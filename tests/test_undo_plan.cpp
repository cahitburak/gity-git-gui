// Undo must never throw away work done after the operation it undoes. These
// pin the rule: a ref is put back only if it still holds what the operation
// left there.
#include "core/git/UndoPlan.h"

#include <gtest/gtest.h>

using namespace gity::git;

TEST(UndoPlan, AMovedBranchGoesBack) {
    const RefMap before{{"refs/heads/main", "a1"}};
    const RefMap after{{"refs/heads/main", "b2"}};
    const auto plan = planRefRestore(before, after, after);
    ASSERT_EQ(plan.changes.size(), 1u);
    EXPECT_EQ(plan.changes[0].ref, "refs/heads/main");
    EXPECT_EQ(plan.changes[0].from, "b2");
    EXPECT_EQ(plan.changes[0].to, "a1");
    EXPECT_TRUE(plan.skipped.empty());
}

TEST(UndoPlan, ADeletedBranchComesBackAndACreatedOneGoes) {
    const RefMap before{{"refs/heads/old", "a1"}};
    const RefMap after{{"refs/heads/new", "c3"}};
    const auto plan = planRefRestore(before, after, after);
    ASSERT_EQ(plan.changes.size(), 2u);
    EXPECT_EQ(plan.changes[0].ref, "refs/heads/new");
    EXPECT_TRUE(plan.changes[0].to.empty()) << "created by the operation: deleted";
    EXPECT_EQ(plan.changes[1].ref, "refs/heads/old");
    EXPECT_TRUE(plan.changes[1].from.empty()) << "deleted by the operation: recreated";
    EXPECT_EQ(plan.changes[1].to, "a1");
}

TEST(UndoPlan, ABranchThatMovedAgainSinceIsLeftAlone) {
    const RefMap before{{"refs/heads/main", "a1"}};
    const RefMap after{{"refs/heads/main", "b2"}};
    const RefMap current{{"refs/heads/main", "c3"}}; // a commit in a terminal since
    const auto plan = planRefRestore(before, after, current);
    EXPECT_TRUE(plan.changes.empty());
    ASSERT_EQ(plan.skipped.size(), 1u);
    EXPECT_EQ(plan.skipped[0], "refs/heads/main");
}

TEST(UndoPlan, UntouchedRefsAreNotConsidered) {
    const RefMap before{{"refs/heads/main", "a1"}, {"refs/tags/v1", "t1"}};
    const RefMap after{{"refs/heads/main", "b2"}, {"refs/tags/v1", "t1"}};
    RefMap current = after;
    current["refs/tags/v1"] = "t9"; // moved since, but the operation never touched it
    const auto plan = planRefRestore(before, after, current);
    EXPECT_EQ(plan.changes.size(), 1u);
    EXPECT_TRUE(plan.skipped.empty());
}

TEST(UndoPlan, AlreadyRestoredIsNotAChangeOrASkip) {
    const RefMap before{{"refs/heads/main", "a1"}};
    const RefMap after{{"refs/heads/main", "b2"}};
    const auto plan = planRefRestore(before, after, before);
    EXPECT_TRUE(plan.changes.empty());
    EXPECT_TRUE(plan.skipped.empty());
}

TEST(UndoPlan, ADroppedStashIsStoredBackAndAPushedOneDropped) {
    const std::vector<StashEntry> before{{"s2", "second"}, {"s1", "first"}};
    const std::vector<StashEntry> afterDrop{{"s1", "first"}};
    const auto restoreDrop = planStashRestore(before, afterDrop, afterDrop);
    ASSERT_EQ(restoreDrop.store.size(), 1u);
    EXPECT_EQ(restoreDrop.store[0].oid, "s2");
    EXPECT_TRUE(restoreDrop.drop.empty());

    const std::vector<StashEntry> afterPush{{"s3", "new"}, {"s2", "second"}, {"s1", "first"}};
    const auto restorePush = planStashRestore(before, afterPush, afterPush);
    EXPECT_TRUE(restorePush.store.empty());
    ASSERT_EQ(restorePush.drop.size(), 1u);
    EXPECT_EQ(restorePush.drop[0], "s3");
}

TEST(UndoPlan, AStashAlreadyDroppedSinceIsNotDroppedAgain) {
    const std::vector<StashEntry> before;
    const std::vector<StashEntry> after{{"s3", "new"}};
    const std::vector<StashEntry> current; // dropped by hand since
    const auto plan = planStashRestore(before, after, current);
    EXPECT_TRUE(plan.drop.empty());
}
