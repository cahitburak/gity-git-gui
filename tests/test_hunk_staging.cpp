// The arithmetic behind "· 2 of 4 lines staged".
//
// Built by hand rather than from a repository: the question is whether two
// diffs are lined up correctly, and hand-built diffs state the alignment
// being tested instead of hiding it behind whatever git happened to produce.
#include "core/git/HunkStaging.h"

#include <gtest/gtest.h>

using namespace gity::git;

namespace {

struct LineSpec {
    DiffLineKind kind;
    std::uint32_t oldLine;
    std::uint32_t newLine;
};

/// One hunk covering worktree lines [newStart, newStart + newCount).
FileDiff makeDiff(int newStart, int newCount, const std::vector<LineSpec>& specs) {
    FileDiff diff;
    diff.path = "Assets/Player.cs";

    DiffHunk hunk;
    hunk.oldStart = newStart;
    hunk.oldCount = newCount;
    hunk.newStart = newStart;
    hunk.newCount = newCount;
    hunk.firstLine = 0;
    hunk.lineCount = static_cast<std::uint32_t>(specs.size());
    diff.hunks.push_back(hunk);

    for (const LineSpec& spec : specs) {
        DiffLine line;
        line.kind = spec.kind;
        line.oldLine = spec.oldLine;
        line.newLine = spec.newLine;
        diff.lines.push_back(line);
    }
    return diff;
}

constexpr auto kAdd = DiffLineKind::Addition;
constexpr auto kDel = DiffLineKind::Deletion;
constexpr auto kCtx = DiffLineKind::Context;

} // namespace

TEST(HunkStagingCounts, NothingStagedWhenBothSidesAgree) {
    // Every change in the region is still to stage.
    const FileDiff unstaged = makeDiff(10, 3, {{kCtx, 10, 10}, {kAdd, 0, 11}, {kAdd, 0, 12}});
    const FileDiff combined = makeDiff(10, 3, {{kCtx, 10, 10}, {kAdd, 0, 11}, {kAdd, 0, 12}});

    const auto counts = hunkStagingCounts(unstaged, combined);
    ASSERT_EQ(counts.size(), 1u);
    EXPECT_EQ(counts[0].staged, 0u);
    EXPECT_EQ(counts[0].total, 2u);
    EXPECT_FALSE(counts[0].partial());
}

TEST(HunkStagingCounts, TheDifferenceBetweenTheTwoDiffsIsWhatIsStaged) {
    // Four changes to the region overall; two of them are no longer pending,
    // so two are already in the index.
    const FileDiff unstaged = makeDiff(10, 4, {{kCtx, 10, 10}, {kAdd, 0, 11}, {kAdd, 0, 12}});
    const FileDiff combined = makeDiff(
        10, 4, {{kCtx, 10, 10}, {kAdd, 0, 11}, {kAdd, 0, 12}, {kAdd, 0, 13}, {kAdd, 0, 13}});

    const auto counts = hunkStagingCounts(unstaged, combined);
    ASSERT_EQ(counts.size(), 1u);
    EXPECT_EQ(counts[0].total, 4u);
    EXPECT_EQ(counts[0].staged, 2u);
    EXPECT_TRUE(counts[0].partial());
}

TEST(HunkStagingCounts, ChangesOutsideTheRegionAreNotCounted) {
    // The combined diff covers the whole file; a hunk must only account for
    // its own region, or every hunk reports the file's total and the number
    // means nothing.
    const FileDiff unstaged = makeDiff(10, 2, {{kAdd, 0, 10}});
    FileDiff combined = makeDiff(10, 2, {{kAdd, 0, 10}});
    // A second, distant hunk in the combined diff.
    DiffHunk far;
    far.newStart = 200;
    far.newCount = 2;
    far.firstLine = static_cast<std::uint32_t>(combined.lines.size());
    far.lineCount = 2;
    combined.hunks.push_back(far);
    combined.lines.push_back({kAdd, 0, 200, 0, 0});
    combined.lines.push_back({kAdd, 0, 201, 0, 0});

    const auto counts = hunkStagingCounts(unstaged, combined);
    ASSERT_EQ(counts.size(), 1u);
    EXPECT_EQ(counts[0].total, 1u);
    EXPECT_EQ(counts[0].staged, 0u);
}

TEST(HunkStagingCounts, DeletionsAreAnchoredSoTheyStillBelongToARegion) {
    // A deletion has no worktree line of its own. Anchored to the position it
    // was removed from, it still falls inside the hunk; unanchored it would
    // land at line 0 and a hunk that only removes lines would always report
    // nothing staged.
    const FileDiff unstaged = makeDiff(10, 1, {{kCtx, 10, 10}, {kDel, 11, 0}});
    const FileDiff combined =
        makeDiff(10, 1, {{kCtx, 10, 10}, {kDel, 11, 0}, {kDel, 12, 0}});

    const auto counts = hunkStagingCounts(unstaged, combined);
    ASSERT_EQ(counts.size(), 1u);
    EXPECT_EQ(counts[0].total, 2u);
    EXPECT_EQ(counts[0].staged, 1u);
}

TEST(HunkStagingCounts, AnEmptyCombinedDiffReportsNothingStaged) {
    // Nothing loaded it, or the file is untracked. "Nothing here is staged" is
    // the true answer, not a count that undershoots the total.
    const FileDiff unstaged = makeDiff(10, 2, {{kAdd, 0, 10}, {kAdd, 0, 11}});
    const auto counts = hunkStagingCounts(unstaged, FileDiff{});
    ASSERT_EQ(counts.size(), 1u);
    EXPECT_EQ(counts[0].staged, 0u);
    EXPECT_EQ(counts[0].total, 2u);
}

TEST(HunkStagingCounts, OneEntryPerHunkInOrder) {
    FileDiff unstaged = makeDiff(10, 1, {{kAdd, 0, 10}});
    DiffHunk second;
    second.newStart = 50;
    second.newCount = 1;
    second.firstLine = 1;
    second.lineCount = 1;
    unstaged.hunks.push_back(second);
    unstaged.lines.push_back({kAdd, 0, 50, 0, 0});

    const auto counts = hunkStagingCounts(unstaged, unstaged);
    ASSERT_EQ(counts.size(), 2u);
    EXPECT_EQ(counts[0].total, 1u);
    EXPECT_EQ(counts[1].total, 1u);
}

TEST(HunkStagingCounts, TheCountNeverUnderflows) {
    // If the two diffs were ever loaded a moment apart, the combined one could
    // be missing a change the unstaged one has. Clamped rather than wrapped
    // around to an enormous number.
    const FileDiff unstaged = makeDiff(10, 3, {{kAdd, 0, 10}, {kAdd, 0, 11}, {kAdd, 0, 12}});
    const FileDiff combined = makeDiff(10, 3, {{kAdd, 0, 10}});

    const auto counts = hunkStagingCounts(unstaged, combined);
    ASSERT_EQ(counts.size(), 1u);
    EXPECT_EQ(counts[0].staged, 0u);
    EXPECT_EQ(counts[0].total, 3u);
}
