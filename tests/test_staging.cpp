// M2 — partial-patch synthesis.
//
// libgit2 can apply a patch to the index but has no notion of "these lines
// only", so a selection has to become a patch first. That transformation is
// the same one `git add -p` performs, and getting it wrong corrupts the index
// silently rather than failing loudly — so it is pinned here in detail.

#include "core/git/Staging.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using namespace gity::git;

struct LineSpec {
    DiffLineKind kind;
    const char* text;
    std::uint32_t oldLine;
    std::uint32_t newLine;
};

/// Builds a FileDiff by hand so the synthesis can be tested without a
/// repository. Mirrors what loadWorkingDiff produces.
FileDiff makeDiff(int oldStart, int newStart, const std::vector<LineSpec>& specs) {
    FileDiff diff;
    diff.path = "Assets/Player.cs";

    DiffHunk hunk;
    hunk.oldStart = oldStart;
    hunk.newStart = newStart;
    hunk.firstLine = 0;

    for (const auto& spec : specs) {
        DiffLine line;
        line.kind = spec.kind;
        line.oldLine = spec.oldLine;
        line.newLine = spec.newLine;
        line.textOffset = static_cast<std::uint32_t>(diff.text.size());
        line.textLength = static_cast<std::uint32_t>(std::string_view(spec.text).size());
        diff.text += spec.text;
        diff.lines.push_back(line);
    }

    hunk.lineCount = static_cast<std::uint32_t>(diff.lines.size());
    diff.hunks.push_back(hunk);
    return diff;
}

/// One context line, one deletion, one addition, one context line.
FileDiff simpleDiff() {
    return makeDiff(10, 10,
                    {
                        {DiffLineKind::Context, "keep before", 10, 10},
                        {DiffLineKind::Deletion, "old line", 11, 0},
                        {DiffLineKind::Addition, "new line", 0, 11},
                        {DiffLineKind::Context, "keep after", 12, 12},
                    });
}

std::size_t countLinesStartingWith(const std::string& patch, char prefix) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while (pos < patch.size()) {
        const std::size_t eol = patch.find('\n', pos);
        const std::string line = patch.substr(pos, eol - pos);
        if (!line.empty() && line[0] == prefix && line.rfind("+++", 0) != 0 &&
            line.rfind("---", 0) != 0) {
            ++count;
        }
        if (eol == std::string::npos) {
            break;
        }
        pos = eol + 1;
    }
    return count;
}

TEST(Staging, SelectingEverythingReproducesTheChange) {
    const FileDiff diff = simpleDiff();
    const LineSelection all = linesOfHunk(diff, 0);
    ASSERT_EQ(all.size(), 2u) << "only the changed lines, not context";

    const std::string patch = buildPartialPatch(diff, all, false);
    // git_diff_from_buffer rejects a patch without the "diff --git" line, so
    // its presence is part of the contract, not cosmetic.
    EXPECT_NE(patch.find("diff --git a/Assets/Player.cs b/Assets/Player.cs"), std::string::npos)
        << patch;
    EXPECT_NE(patch.find("--- a/Assets/Player.cs"), std::string::npos);
    EXPECT_NE(patch.find("+++ b/Assets/Player.cs"), std::string::npos);
    EXPECT_NE(patch.find("-old line"), std::string::npos);
    EXPECT_NE(patch.find("+new line"), std::string::npos);
    // 2 context + 1 deletion = 3 old lines; 2 context + 1 addition = 3 new.
    EXPECT_NE(patch.find("@@ -10,3 +10,3 @@"), std::string::npos) << patch;
}

TEST(Staging, EmptySelectionProducesNoPatch) {
    EXPECT_TRUE(buildPartialPatch(simpleDiff(), {}, false).empty());
}

TEST(Staging, UnselectedAdditionsAreDropped) {
    const FileDiff diff = simpleDiff();
    LineSelection onlyDeletion;
    onlyDeletion.insert(1); // the deletion

    const std::string patch = buildPartialPatch(diff, onlyDeletion, false);
    EXPECT_NE(patch.find("-old line"), std::string::npos);
    EXPECT_EQ(patch.find("+new line"), std::string::npos)
        << "an unselected addition is not part of the result";
    // old: 2 context + 1 deletion = 3; new: 2 context = 2.
    EXPECT_NE(patch.find("@@ -10,3 +10,2 @@"), std::string::npos) << patch;
}

TEST(Staging, UnselectedDeletionsBecomeContext) {
    // The critical case: dropping an unselected deletion instead of keeping it
    // as context would silently delete the line from the staged result.
    const FileDiff diff = simpleDiff();
    LineSelection onlyAddition;
    onlyAddition.insert(2); // the addition

    const std::string patch = buildPartialPatch(diff, onlyAddition, false);
    EXPECT_NE(patch.find("+new line"), std::string::npos);
    EXPECT_EQ(patch.find("-old line"), std::string::npos);
    EXPECT_NE(patch.find(" old line"), std::string::npos)
        << "the unselected deletion must survive as context";
    // old: 3 context; new: 3 context + 1 addition = 4.
    EXPECT_NE(patch.find("@@ -10,3 +10,4 @@"), std::string::npos) << patch;
}

TEST(Staging, HunksWithNothingSelectedAreOmitted) {
    FileDiff diff = simpleDiff();

    // A second hunk, entirely unselected.
    DiffHunk second;
    second.oldStart = 50;
    second.newStart = 50;
    second.firstLine = static_cast<std::uint32_t>(diff.lines.size());
    DiffLine line;
    line.kind = DiffLineKind::Addition;
    line.newLine = 50;
    line.textOffset = static_cast<std::uint32_t>(diff.text.size());
    line.textLength = 8;
    diff.text += "untouched";
    diff.lines.push_back(line);
    second.lineCount = 1;
    diff.hunks.push_back(second);

    const std::string patch = buildPartialPatch(diff, linesOfHunk(diff, 0), false);
    EXPECT_NE(patch.find("@@ -10,"), std::string::npos);
    EXPECT_EQ(patch.find("@@ -50,"), std::string::npos) << "empty hunk must not be emitted";
}

TEST(Staging, ReversalInvertsBodyAndHeader) {
    // Reversing only the file headers is the tempting shortcut, and it produces
    // a patch that claims one direction and contains the other — libgit2 will
    // apply it happily and unstaging would stage more.
    const FileDiff diff = simpleDiff();
    const LineSelection all = linesOfHunk(diff, 0);

    const std::string forward = buildPartialPatch(diff, all, false);
    const std::string reversed = buildPartialPatch(diff, all, true);

    EXPECT_NE(forward, reversed);
    EXPECT_NE(reversed.find("+old line"), std::string::npos) << reversed;
    EXPECT_NE(reversed.find("-new line"), std::string::npos) << reversed;
    EXPECT_EQ(countLinesStartingWith(forward, '+'), countLinesStartingWith(reversed, '-'));
    EXPECT_EQ(countLinesStartingWith(forward, '-'), countLinesStartingWith(reversed, '+'));
}

TEST(Staging, ReversedHunkHeaderSwapsSides) {
    const FileDiff diff = makeDiff(10, 20,
                                   {
                                       {DiffLineKind::Context, "ctx", 10, 20},
                                       {DiffLineKind::Addition, "added", 0, 21},
                                   });
    const std::string reversed = buildPartialPatch(diff, linesOfHunk(diff, 0), true);
    // Forward is "@@ -10,1 +20,2 @@", so reversed must be "@@ -20,2 +10,1 @@".
    EXPECT_NE(reversed.find("@@ -20,2 +10,1 @@"), std::string::npos) << reversed;
}

TEST(Staging, ContextIsPreservedInBothDirections) {
    const FileDiff diff = simpleDiff();
    const LineSelection all = linesOfHunk(diff, 0);
    for (const bool reverse : {false, true}) {
        const std::string patch = buildPartialPatch(diff, all, reverse);
        EXPECT_NE(patch.find(" keep before"), std::string::npos) << patch;
        EXPECT_NE(patch.find(" keep after"), std::string::npos) << patch;
    }
}

TEST(Staging, EmptyDiffProducesNoPatch) {
    const FileDiff empty;
    EXPECT_TRUE(buildPartialPatch(empty, {}, false).empty());
}

TEST(Staging, RenameUsesTheOldPathOnTheOldSide) {
    FileDiff diff = simpleDiff();
    diff.oldPath = "Assets/Old.cs";

    const std::string patch = buildPartialPatch(diff, linesOfHunk(diff, 0), false);
    EXPECT_NE(patch.find("diff --git a/Assets/Old.cs b/Assets/Player.cs"), std::string::npos)
        << patch;
    EXPECT_NE(patch.find("--- a/Assets/Old.cs"), std::string::npos) << patch;
    EXPECT_NE(patch.find("+++ b/Assets/Player.cs"), std::string::npos) << patch;
}

} // namespace
