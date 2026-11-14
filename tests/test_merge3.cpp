// The three-way split a conflict editor works from, and the result it builds.
#include "core/git/Merge3.h"

#include <git2.h>
#include <gtest/gtest.h>

using gity::git::Lines;
using gity::git::Merge3;
using gity::git::MergeRegion;
using gity::git::MergeResolution;
using gity::git::Pick;
using Kind = MergeRegion::Kind;

namespace {

class Merge3Test : public ::testing::Test {
protected:
    void SetUp() override { git_libgit2_init(); }

    void TearDown() override { git_libgit2_shutdown(); }

    static std::vector<Kind> kinds(const Merge3& merge) {
        std::vector<Kind> out;
        for (const auto& region : merge.regions) {
            out.push_back(region.kind);
        }
        return out;
    }
};

const char* kBase = "a\nb\nc\nd\ne\nf\ng\n";

} // namespace

TEST_F(Merge3Test, SplitsLinesKeepingTheirEndings) {
    EXPECT_EQ(gity::git::splitLines("a\r\nb\nc"), (Lines{"a\r\n", "b\n", "c"}));
    EXPECT_TRUE(gity::git::splitLines("").empty());
}

TEST_F(Merge3Test, NothingChangedIsOneRegion) {
    const Merge3 merge = gity::git::mergeLines(kBase, kBase, kBase);
    EXPECT_EQ(kinds(merge), (std::vector<Kind>{Kind::Same}));
    EXPECT_EQ(MergeResolution(merge).text(), kBase);
}

TEST_F(Merge3Test, ChangesOnOneSideEachAreTakenFromThatSide) {
    const Merge3 merge =
        gity::git::mergeLines(kBase, "a\nB\nc\nd\ne\nf\ng\n", "a\nb\nc\nd\ne\nF\nF2\ng\n");
    EXPECT_EQ(kinds(merge),
              (std::vector<Kind>{Kind::Same, Kind::Ours, Kind::Same, Kind::Theirs, Kind::Same}));
    EXPECT_EQ(merge.conflictCount(), 0U);
    const MergeResolution resolution(merge);
    EXPECT_EQ(resolution.unresolvedCount(), 0U);
    EXPECT_EQ(resolution.text(), "a\nB\nc\nd\ne\nF\nF2\ng\n");
}

TEST_F(Merge3Test, TheSameChangeOnBothSidesIsTakenOnce) {
    const Merge3 merge =
        gity::git::mergeLines(kBase, "a\nb\nC\nd\ne\nf\ng\n", "a\nb\nC\nd\ne\nf\ng\n");
    EXPECT_EQ(kinds(merge), (std::vector<Kind>{Kind::Same, Kind::Both, Kind::Same}));
    EXPECT_EQ(MergeResolution(merge).text(), "a\nb\nC\nd\ne\nf\ng\n");
}

TEST_F(Merge3Test, DifferentChangesToTheSameLinesConflict) {
    const Merge3 merge =
        gity::git::mergeLines(kBase, "a\nb\nOURS\nd\ne\nf\ng\n", "a\nb\nTHEIRS\nTWO\nd\ne\nf\ng\n");
    ASSERT_EQ(kinds(merge), (std::vector<Kind>{Kind::Same, Kind::Conflict, Kind::Same}));
    const MergeRegion& conflict = merge.regions[1];
    EXPECT_EQ(conflict.base, (Lines{"c\n"}));
    EXPECT_EQ(conflict.ours, (Lines{"OURS\n"}));
    EXPECT_EQ(conflict.theirs, (Lines{"THEIRS\n", "TWO\n"}));

    MergeResolution resolution(merge);
    EXPECT_EQ(resolution.unresolvedCount(), 1U);
    EXPECT_EQ(resolution.text("main", "feature"),
              "a\nb\n<<<<<<< main\nOURS\n=======\nTHEIRS\nTWO\n>>>>>>> feature\nd\ne\nf\ng\n");

    resolution.setPick(1, Pick::Theirs);
    EXPECT_EQ(resolution.text(), "a\nb\nTHEIRS\nTWO\nd\ne\nf\ng\n");
    resolution.setPick(1, Pick::OursThenTheirs);
    EXPECT_EQ(resolution.text(), "a\nb\nOURS\nTHEIRS\nTWO\nd\ne\nf\ng\n");
    resolution.setPick(1, Pick::TheirsThenOurs);
    EXPECT_EQ(resolution.text(), "a\nb\nTHEIRS\nTWO\nOURS\nd\ne\nf\ng\n");
    resolution.setPick(1, Pick::Base);
    EXPECT_EQ(resolution.text(), kBase);
    resolution.setEdited(1, "mine\n");
    EXPECT_EQ(resolution.pick(1), Pick::Edited);
    EXPECT_EQ(resolution.text(), "a\nb\nmine\nd\ne\nf\ng\n");
    EXPECT_EQ(resolution.unresolvedCount(), 0U);

    resolution.reset();
    EXPECT_EQ(resolution.pick(1), Pick::Unresolved);
}

TEST_F(Merge3Test, ChangesThatTouchAreOneConflict) {
    // Ours changes c, theirs changes d: next to each other, so git calls it
    // one conflict rather than merging them line by line.
    const Merge3 merge =
        gity::git::mergeLines(kBase, "a\nb\nC\nd\ne\nf\ng\n", "a\nb\nc\nD\ne\nf\ng\n");
    ASSERT_EQ(merge.conflictCount(), 1U);
    const auto& conflict = merge.regions[1];
    EXPECT_EQ(conflict.base, (Lines{"c\n", "d\n"}));
    EXPECT_EQ(conflict.ours, (Lines{"C\n", "d\n"}));
    EXPECT_EQ(conflict.theirs, (Lines{"c\n", "D\n"}));
}

TEST_F(Merge3Test, AdditionsAtTheSamePlaceConflict) {
    const Merge3 merge = gity::git::mergeLines("a\nz\n", "a\nours\nz\n", "a\ntheirs\nz\n");
    ASSERT_EQ(kinds(merge), (std::vector<Kind>{Kind::Same, Kind::Conflict, Kind::Same}));
    EXPECT_TRUE(merge.regions[1].base.empty());
}

TEST_F(Merge3Test, BothAddingAFileIsOneConflict) {
    // No ancestor: an add/add conflict.
    const Merge3 merge = gity::git::mergeLines("", "one\n", "two\n");
    EXPECT_EQ(kinds(merge), (std::vector<Kind>{Kind::Conflict}));
}

TEST_F(Merge3Test, SeveralConflictsAreFoundInTurn) {
    const Merge3 merge =
        gity::git::mergeLines(kBase, "A1\nb\nc\nd\ne\nf\nG1\n", "A2\nb\nc\nd\ne\nf\nG2\n");
    ASSERT_EQ(merge.conflictCount(), 2U);
    MergeResolution resolution(merge);
    const auto first = resolution.nextUnresolved(resolution.regionCount() - 1);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, 0U);
    const auto second = resolution.nextUnresolved(*first);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*second, 2U);
    resolution.pickAllConflicts(Pick::Ours);
    EXPECT_FALSE(resolution.nextUnresolved(0).has_value());
    EXPECT_EQ(resolution.text(), "A1\nb\nc\nd\ne\nf\nG1\n");
}

TEST_F(Merge3Test, KeepsTheFilesLineEndings) {
    const Merge3 merge =
        gity::git::mergeLines("a\r\nb\r\nc\r\n", "a\r\nOURS\r\nc\r\n", "a\r\nTHEIRS\r\nc\r\n");
    const MergeResolution resolution(merge);
    EXPECT_EQ(resolution.text("x", "y"),
              "a\r\n<<<<<<< x\r\nOURS\r\n=======\r\nTHEIRS\r\n>>>>>>> y\r\nc\r\n");
}

TEST_F(Merge3Test, ALastLineWithoutAnEndingIsEndedBeforeAnother) {
    const Merge3 merge = gity::git::mergeLines("a\nb", "a\nours", "a\ntheirs");
    ASSERT_EQ(merge.conflictCount(), 1U);
    MergeResolution resolution(merge);
    resolution.setPick(1, Pick::OursThenTheirs);
    EXPECT_EQ(resolution.text(), "a\nours\ntheirs");
    resolution.setPick(1, Pick::Ours);
    EXPECT_EQ(resolution.text(), "a\nours");
    resolution.setPick(1, Pick::Unresolved);
    EXPECT_EQ(resolution.text("o", "t"), "a\n<<<<<<< o\nours\n=======\ntheirs\n>>>>>>> t\n");
}

TEST_F(Merge3Test, LinesBothSidesAgreeOnAreLeftOutOfTheConflict) {
    // Both added "n1" first; only what follows differs — git's markers leave
    // the shared line out too.
    const Merge3 merge = gity::git::mergeLines("", "n1\n", "n1\nn3\nn2\n");
    ASSERT_EQ(kinds(merge), (std::vector<Kind>{Kind::Both, Kind::Conflict}));
    EXPECT_EQ(merge.regions[0].ours, (Lines{"n1\n"}));
    EXPECT_TRUE(merge.regions[1].ours.empty());
    EXPECT_EQ(merge.regions[1].theirs, (Lines{"n3\n", "n2\n"}));
    EXPECT_EQ(MergeResolution(merge).text(), "n1\n<<<<<<< ours\n=======\nn3\nn2\n>>>>>>> theirs\n");
}
