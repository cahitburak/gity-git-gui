// Staging, unstaging and discarding against real repositories, through git.
//
// Each test here is a defect that pure patch-building tests could not see,
// because the patch looked right and only git applying it showed otherwise.
#include "TempRepo.h"

#include "core/git/Staging.h"
#include "session/IndexWrites.h"

#include <gtest/gtest.h>

namespace {

using gity::git::buildPartialPatch;
using gity::git::DiffSide;
using gity::git::linesOfHunk;
using gity::git::loadWorkingDiff;
using gity::session::index::applyToIndex;
using gity::session::index::applyToWorktree;
using gity::session::index::stagePaths;
using gity::session::index::unstagePaths;
using gity::test::TempRepo;

QByteArray bytes(const std::string& patch) {
    return QByteArray::fromStdString(patch);
}

/// The index of the first line in `diff` whose text is `text`.
std::uint32_t lineNamed(const gity::git::FileDiff& diff, std::string_view text) {
    for (std::uint32_t i = 0; i < diff.lines.size(); ++i) {
        if (diff.lineText(diff.lines[i]) == text) {
            return i;
        }
    }
    ADD_FAILURE() << "no line " << text;
    return 0;
}

} // namespace

TEST(IndexWrites, UnstagingAPathWithGlobCharactersLeavesItsNeighbours) {
    TempRepo repo;
    repo.write("Assets/[id].cs", "one\n");
    repo.write("Assets/d.cs", "one\n");
    repo.commitAll();
    repo.write("Assets/[id].cs", "two\n");
    repo.write("Assets/d.cs", "two\n");
    ASSERT_TRUE(stagePaths(repo.dir(), {"Assets/[id].cs", "Assets/d.cs"}).ok());

    ASSERT_TRUE(unstagePaths(repo.dir(), {"Assets/[id].cs"}).ok());
    EXPECT_EQ(repo.indexContents("Assets/[id].cs"), "one\n");
    EXPECT_EQ(repo.indexContents("Assets/d.cs"), "two\n") << "unstaging [id].cs unstaged d.cs";
}

TEST(IndexWrites, StagingRunsTheUsersCleanFilter) {
    // The reason staging goes through git at all. LFS and git-crypt are clean
    // filters; libgit2 ran none, so the index got the raw file.
    TempRepo repo;
    repo.git({"config", "filter.upper.clean", "tr a-z A-Z"});
    repo.write(".gitattributes", "*.txt filter=upper\n");
    repo.commitAll();
    repo.write("secret.txt", "plain\n");

    ASSERT_TRUE(stagePaths(repo.dir(), {"secret.txt"}).ok());
    EXPECT_EQ(repo.indexContents("secret.txt"), "PLAIN\n");
}

TEST(IndexWrites, StagingADeletedFileStagesTheDeletion) {
    TempRepo repo;
    repo.write("gone.txt", "one\n");
    repo.commitAll();
    repo.remove("gone.txt");

    ASSERT_TRUE(stagePaths(repo.dir(), {"gone.txt"}).ok());
    EXPECT_FALSE(repo.inIndex("gone.txt"));
}

TEST(IndexWrites, UnstagingWorksBeforeTheFirstCommit) {
    TempRepo repo;
    repo.write("first.txt", "one\n");
    ASSERT_TRUE(stagePaths(repo.dir(), {"first.txt"}).ok());

    ASSERT_TRUE(unstagePaths(repo.dir(), {"first.txt"}).ok());
    EXPECT_FALSE(repo.inIndex("first.txt"));
}

TEST(IndexWrites, StagingOneLineOfAHunk) {
    TempRepo repo;
    repo.write("f.txt", "a\nd\n");
    repo.commitAll();
    repo.write("f.txt", "a\nb\nc\nd\n");

    const auto diff = loadWorkingDiff(repo.repository(), "f.txt", DiffSide::Unstaged);
    const std::string patch = buildPartialPatch(diff, {lineNamed(diff, "c")}, false);
    const auto result = applyToIndex(repo.dir(), bytes(patch));
    ASSERT_TRUE(result.ok()) << result.errorOutput.toStdString() << patch;
    EXPECT_EQ(repo.indexContents("f.txt"), "a\nc\nd\n");
}

TEST(IndexWrites, UnstagingOneLineOfAHunk) {
    // Staged: +b +c. Unstaging only +b used to build the patch the staging
    // way and then invert it, which expected the index to lack c.
    TempRepo repo;
    repo.write("f.txt", "a\nd\n");
    repo.commitAll();
    repo.write("f.txt", "a\nb\nc\nd\n");
    ASSERT_TRUE(stagePaths(repo.dir(), {"f.txt"}).ok());

    const auto diff = loadWorkingDiff(repo.repository(), "f.txt", DiffSide::Staged);
    const std::string patch = buildPartialPatch(diff, {lineNamed(diff, "b")}, true);
    const auto result = applyToIndex(repo.dir(), bytes(patch));
    ASSERT_TRUE(result.ok()) << result.errorOutput.toStdString() << patch;
    EXPECT_EQ(repo.indexContents("f.txt"), "a\nc\nd\n");
}

TEST(IndexWrites, UnstagingOneDeletionOfAHunk) {
    TempRepo repo;
    repo.write("f.txt", "a\nb\nc\nd\n");
    repo.commitAll();
    repo.write("f.txt", "a\nd\n");
    ASSERT_TRUE(stagePaths(repo.dir(), {"f.txt"}).ok());

    const auto diff = loadWorkingDiff(repo.repository(), "f.txt", DiffSide::Staged);
    const std::string patch = buildPartialPatch(diff, {lineNamed(diff, "c")}, true);
    const auto result = applyToIndex(repo.dir(), bytes(patch));
    ASSERT_TRUE(result.ok()) << result.errorOutput.toStdString() << patch;
    EXPECT_EQ(repo.indexContents("f.txt"), "a\nc\nd\n");
}

TEST(IndexWrites, StagingSomeLinesOfAnUntrackedFile) {
    // The patch has to say it creates the file: the index has no entry to
    // apply "--- a/new.txt" against.
    TempRepo repo;
    repo.write("base.txt", "x\n");
    repo.commitAll();
    repo.write("new.txt", "one\ntwo\nthree\n");

    const auto diff = loadWorkingDiff(repo.repository(), "new.txt", DiffSide::Unstaged);
    const std::string patch =
        buildPartialPatch(diff, {lineNamed(diff, "one"), lineNamed(diff, "three")}, false);
    const auto result = applyToIndex(repo.dir(), bytes(patch));
    ASSERT_TRUE(result.ok()) << result.errorOutput.toStdString() << patch;
    EXPECT_EQ(repo.indexContents("new.txt"), "one\nthree\n");
}

TEST(IndexWrites, UnstagingLinesOfAStagedDeletionPutsThemBack) {
    TempRepo repo;
    repo.write("gone.txt", "one\ntwo\n");
    repo.commitAll();
    repo.remove("gone.txt");
    ASSERT_TRUE(stagePaths(repo.dir(), {"gone.txt"}).ok());

    const auto diff = loadWorkingDiff(repo.repository(), "gone.txt", DiffSide::Staged);
    const std::string patch = buildPartialPatch(diff, {lineNamed(diff, "two")}, true);
    const auto result = applyToIndex(repo.dir(), bytes(patch));
    ASSERT_TRUE(result.ok()) << result.errorOutput.toStdString() << patch;
    EXPECT_EQ(repo.indexContents("gone.txt"), "two\n");
}

TEST(IndexWrites, StagingALineOfACrLfFile) {
    // The diff view shows lines without their CR; the patch must put it back
    // or it does not match a file committed with CR LF endings.
    TempRepo repo;
    repo.write("win.txt", "a\r\nb\r\nc\r\n");
    repo.commitAll();
    repo.write("win.txt", "a\r\nB\r\nc\r\nD\r\n");

    const auto diff = loadWorkingDiff(repo.repository(), "win.txt", DiffSide::Unstaged);
    const std::string patch =
        buildPartialPatch(diff, {lineNamed(diff, "b"), lineNamed(diff, "B")}, false);
    const auto result = applyToIndex(repo.dir(), bytes(patch));
    ASSERT_TRUE(result.ok()) << result.errorOutput.toStdString() << patch;
    EXPECT_EQ(repo.indexContents("win.txt"), "a\r\nB\r\nc\r\n");
}

TEST(IndexWrites, DiscardingAnUnstagedHunkRewritesTheFile) {
    TempRepo repo;
    repo.write("Assets/Player.cs", "one\n");
    repo.commitAll();
    repo.write("Assets/Player.cs", "one\ntwo\n");

    const auto diff = loadWorkingDiff(repo.repository(), "Assets/Player.cs", DiffSide::Unstaged);
    ASSERT_EQ(diff.hunks.size(), 1u);
    const std::string patch = buildPartialPatch(diff, linesOfHunk(diff, 0), true);
    const auto result = applyToWorktree(repo.dir(), bytes(patch));
    ASSERT_TRUE(result.ok()) << result.errorOutput.toStdString();
    EXPECT_EQ(repo.read("Assets/Player.cs"), "one\n");
}

TEST(IndexWrites, DiscardingADeletedLineThatLooksLikeAPatchHeader) {
    // Deleting "-- comment" makes the patch line "--- comment", which the old
    // reversal skipped as if it were a file header.
    TempRepo repo;
    repo.write("q.sql", "select 1;\n-- comment\nselect 2;\n");
    repo.commitAll();
    repo.write("q.sql", "select 1;\nselect 2;\n");

    const auto diff = loadWorkingDiff(repo.repository(), "q.sql", DiffSide::Unstaged);
    const std::string patch = buildPartialPatch(diff, linesOfHunk(diff, 0), true);
    const auto result = applyToWorktree(repo.dir(), bytes(patch));
    ASSERT_TRUE(result.ok()) << result.errorOutput.toStdString() << patch;
    EXPECT_EQ(repo.read("q.sql"), "select 1;\n-- comment\nselect 2;\n");
}

TEST(IndexWrites, DiscardingOneOfTwoAddedLinesKeepsTheOther) {
    TempRepo repo;
    repo.write("f.txt", "a\nd\n");
    repo.commitAll();
    repo.write("f.txt", "a\nb\nc\nd\n");

    const auto diff = loadWorkingDiff(repo.repository(), "f.txt", DiffSide::Unstaged);
    const std::string patch = buildPartialPatch(diff, {lineNamed(diff, "b")}, true);
    const auto result = applyToWorktree(repo.dir(), bytes(patch));
    ASSERT_TRUE(result.ok()) << result.errorOutput.toStdString() << patch;
    EXPECT_EQ(repo.read("f.txt"), "a\nc\nd\n");
}
