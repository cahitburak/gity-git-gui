#include "core/git/RepoAccount.h"

#include <gtest/gtest.h>

using namespace gity::git;

TEST(RepoAccount, TheHelperAsksGitHubCliForTheNamedAccount) {
    const std::string helper = ghAccountHelper("github.com", "alex-work");
    EXPECT_NE(helper.find("gh auth token --hostname github.com --user alex-work"),
              std::string::npos);
    EXPECT_NE(helper.find("echo username=alex-work"), std::string::npos);
    EXPECT_EQ(helper.rfind("!", 0), 0u) << "a shell helper, as git expects";
    EXPECT_EQ(accountOfGhHelper(helper), "alex-work") << "round trip";
}

TEST(RepoAccount, NamesThatCouldEscapeTheCommandAreRefused) {
    EXPECT_TRUE(ghAccountHelper("github.com", "a;rm -rf ~").empty());
    EXPECT_TRUE(ghAccountHelper("github.com", "$(id)").empty());
    EXPECT_TRUE(ghAccountHelper("github.com", "name with space").empty());
    EXPECT_TRUE(ghAccountHelper("evil.com;x", "someone").empty());
    EXPECT_TRUE(ghAccountHelper("github.com", "").empty());
    EXPECT_TRUE(isSafeAccountName("alex-personal"));
    EXPECT_FALSE(isSafeAccountName("alex.doe@example.com"));
}

TEST(RepoAccount, OtherHelpersAreNotMistakenForOurs) {
    EXPECT_TRUE(accountOfGhHelper("!/usr/bin/gh auth git-credential").empty());
    EXPECT_TRUE(accountOfGhHelper("/usr/lib/git-core/git-credential-libsecret").empty());
    EXPECT_TRUE(accountOfGhHelper("").empty());
}
