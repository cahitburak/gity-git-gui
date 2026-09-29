#include "core/git/MergePrediction.h"

#include <gtest/gtest.h>

#include <string>

using namespace gity::git;

TEST(MergePrediction, ACleanMergeHasNoConflicts) {
    const std::string output("bd56cd12cf0c3c6417703724872cd887705f3ba4\0", 41);
    const auto prediction = parseMergeTree(0, output);
    EXPECT_TRUE(prediction.available);
    EXPECT_TRUE(prediction.clean);
    EXPECT_TRUE(prediction.conflicts.empty());
}

TEST(MergePrediction, ConflictedPathsAreListedOnceEach) {
    // As git 2.55 prints it for two conflicted files; a path can repeat.
    const std::string output("b7c3735e\0f\0g\0f\0", 15);
    const auto prediction = parseMergeTree(1, output);
    EXPECT_TRUE(prediction.available);
    EXPECT_FALSE(prediction.clean);
    ASSERT_EQ(prediction.conflicts.size(), 2u);
    EXPECT_EQ(prediction.conflicts[0], "f");
    EXPECT_EQ(prediction.conflicts[1], "g");
}

TEST(MergePrediction, PathsWithSpacesAndNewlinesSurvive) {
    const std::string output("tree\0Assets/My Scene.unity\0odd\nname\0", 37);
    const auto prediction = parseMergeTree(1, output);
    ASSERT_EQ(prediction.conflicts.size(), 2u);
    EXPECT_EQ(prediction.conflicts[0], "Assets/My Scene.unity");
    EXPECT_EQ(prediction.conflicts[1], "odd\nname");
}

TEST(MergePrediction, AFailureToRunPredictsNothing) {
    const auto prediction = parseMergeTree(128, "fatal: unknown option");
    EXPECT_FALSE(prediction.available);
}

TEST(MergePrediction, NeedsGit238) {
    EXPECT_FALSE(mergeTreeSupported(2, 37));
    EXPECT_TRUE(mergeTreeSupported(2, 38));
    EXPECT_TRUE(mergeTreeSupported(2, 55));
    EXPECT_TRUE(mergeTreeSupported(3, 0));
}
