// Reading `git blame --porcelain`.
#include "core/git/Blame.h"

#include <gtest/gtest.h>

namespace {

const std::string kA = "1111111111111111111111111111111111111111";
const std::string kB = "2222222222222222222222222222222222222222";
const std::string kZero = "0000000000000000000000000000000000000000";

} // namespace

TEST(Blame, ReadsCommitsOnceAndEveryLine) {
    // As git writes it: headers only on a commit's first entry, and a group
    // count only on the first line of each run.
    const std::string output = kA +
                               " 1 1 2\n"
                               "author Ada\n"
                               "author-mail <ada@invalid>\n"
                               "author-time 1700000000\n"
                               "author-tz +0000\n"
                               "committer Ada\n"
                               "summary First version\n"
                               "boundary\n"
                               "filename old.txt\n"
                               "\tone\n" +
                               kA +
                               " 2 2\n"
                               "\ttwo\n" +
                               kB +
                               " 2 3 1\n"
                               "author Bo\n"
                               "author-time 1700000500\n"
                               "summary Add a line with a\ttab\n"
                               "previous " +
                               kA +
                               " old.txt\n"
                               "filename new name.txt\n"
                               "\t\tindented\n" +
                               kA +
                               " 3 4 1\n"
                               "\t\n";
    const gity::git::Blame blame = gity::git::parseBlamePorcelain(output);

    ASSERT_EQ(blame.commits.size(), 2u);
    EXPECT_EQ(blame.commits[0].id, kA);
    EXPECT_EQ(blame.commits[0].author, "Ada");
    EXPECT_EQ(blame.commits[0].when, 1700000000);
    EXPECT_EQ(blame.commits[0].summary, "First version");
    EXPECT_EQ(blame.commits[0].path, "old.txt");
    EXPECT_TRUE(blame.commits[0].previousId.empty());
    EXPECT_EQ(blame.commits[1].author, "Bo");
    EXPECT_EQ(blame.commits[1].summary, "Add a line with a\ttab");
    EXPECT_EQ(blame.commits[1].path, "new name.txt");
    EXPECT_EQ(blame.commits[1].previousId, kA);
    EXPECT_EQ(blame.commits[1].previousPath, "old.txt");

    ASSERT_EQ(blame.lines.size(), 4u);
    EXPECT_EQ(blame.lines[0].text, "one");
    EXPECT_EQ(blame.lines[1].commit, 0u);
    EXPECT_EQ(blame.lines[1].originalLine, 2u);
    // Only the first tab is git's; the rest is the line.
    EXPECT_EQ(blame.lines[2].text, "\tindented");
    EXPECT_EQ(blame.lines[2].commit, 1u);
    EXPECT_EQ(blame.lines[3].text, "");
    EXPECT_EQ(blame.lines[3].commit, 0u);
    EXPECT_EQ(blame.lines[3].originalLine, 3u);
}

TEST(Blame, KnowsUncommittedLines) {
    const std::string output = kZero + " 1 1 1\n"
                                       "author Not Committed Yet\n"
                                       "summary Version of a.txt from a.txt\n"
                                       "filename a.txt\n"
                                       "\tedited\n";
    const gity::git::Blame blame = gity::git::parseBlamePorcelain(output);
    ASSERT_EQ(blame.commits.size(), 1u);
    EXPECT_TRUE(blame.commits[0].uncommitted());
    ASSERT_EQ(blame.lines.size(), 1u);
    gity::git::BlameCommit committed;
    committed.id = kA;
    EXPECT_FALSE(committed.uncommitted());
}

TEST(Blame, StopsAtAMalformedTail) {
    const std::string output = kA + " 1 1 1\n"
                                    "author Ada\n"
                                    "\tkept\n"
                                    "not a header\n"
                                    "\tlost\n";
    const gity::git::Blame blame = gity::git::parseBlamePorcelain(output);
    ASSERT_EQ(blame.lines.size(), 1u);
    EXPECT_EQ(blame.lines[0].text, "kept");
}

TEST(Blame, EmptyOutputIsAnEmptyFile) {
    const gity::git::Blame blame = gity::git::parseBlamePorcelain("");
    EXPECT_TRUE(blame.commits.empty());
    EXPECT_TRUE(blame.lines.empty());
}
