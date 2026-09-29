#include "core/git/PullRequest.h"

#include <gtest/gtest.h>

using gity::git::newPullRequestUrl;
using gity::git::Provider;
using gity::git::repositoryWebUrl;

TEST(RepositoryWebUrl, EveryRemoteShapeGivesTheSamePage) {
    const std::string page = "https://github.com/acme-games/space-game";
    EXPECT_EQ(repositoryWebUrl("https://github.com/acme-games/space-game.git"), page);
    EXPECT_EQ(repositoryWebUrl("https://someone@github.com/acme-games/space-game.git"), page);
    EXPECT_EQ(repositoryWebUrl("git@github.com:acme-games/space-game.git"), page);
    EXPECT_EQ(repositoryWebUrl("ssh://git@github.com/acme-games/space-game"), page);
    EXPECT_EQ(repositoryWebUrl("  https://github.com/acme-games/space-game/  "), page);
}

TEST(RepositoryWebUrl, PortsAndNesting) {
    // An https port is the web server's; an ssh one is not.
    EXPECT_EQ(repositoryWebUrl("https://gitlab.studio.io:8443/games/tools/build.git"),
              "https://gitlab.studio.io:8443/games/tools/build");
    EXPECT_EQ(repositoryWebUrl("ssh://git@gitlab.studio.io:2222/games/tools/build.git"),
              "https://gitlab.studio.io/games/tools/build");
    // An email as the user, unescaped: the host is after the last '@'.
    EXPECT_EQ(repositoryWebUrl("https://a@b.io@github.com/o/r.git"), "https://github.com/o/r");
}

TEST(RepositoryWebUrl, AzureSshMapsToTheWebLayout) {
    EXPECT_EQ(repositoryWebUrl("git@ssh.dev.azure.com:v3/org/project/repo"),
              "https://dev.azure.com/org/project/_git/repo");
    EXPECT_EQ(repositoryWebUrl("https://org@dev.azure.com/org/project/_git/repo"),
              "https://dev.azure.com/org/project/_git/repo");
}

TEST(RepositoryWebUrl, NothingForLocalPaths) {
    EXPECT_EQ(repositoryWebUrl("/srv/git/repo.git"), "");
    EXPECT_EQ(repositoryWebUrl("file:///srv/git/repo.git"), "");
    EXPECT_EQ(repositoryWebUrl("../repo.git"), "");
    EXPECT_EQ(repositoryWebUrl(""), "");
}

TEST(NewPullRequestUrl, EachProvidersPage) {
    EXPECT_EQ(newPullRequestUrl(Provider::GitHub, "git@github.com:o/r.git", "feature/x",
                                "Release/200.150.0"),
              "https://github.com/o/r/compare/Release/200.150.0...feature/x?expand=1");
    EXPECT_EQ(newPullRequestUrl(Provider::GitHub, "git@github.com:o/r.git", "feature/x", ""),
              "https://github.com/o/r/compare/feature/x?expand=1");
    EXPECT_EQ(newPullRequestUrl(Provider::GitLab, "https://gitlab.com/g/s/r.git", "feature/x",
                                "main"),
              "https://gitlab.com/g/s/r/-/merge_requests/new?merge_request%5Bsource_branch%5D="
              "feature%2Fx&merge_request%5Btarget_branch%5D=main");
    EXPECT_EQ(newPullRequestUrl(Provider::Bitbucket, "git@bitbucket.org:o/r.git", "fix", "dev"),
              "https://bitbucket.org/o/r/pull-requests/new?source=fix&dest=dev");
    EXPECT_EQ(newPullRequestUrl(Provider::Gitea, "https://gitea.com/o/r.git", "fix", "main"),
              "https://gitea.com/o/r/compare/main...fix");
    EXPECT_EQ(newPullRequestUrl(Provider::AzureDevOps, "git@ssh.dev.azure.com:v3/o/p/r", "fix",
                                "main"),
              "https://dev.azure.com/o/p/_git/r/pullrequestcreate?sourceRef=fix&targetRef=main");
}

TEST(NewPullRequestUrl, BranchNamesAreEncoded) {
    // '#' would end the path and '+' would read as a space.
    EXPECT_EQ(newPullRequestUrl(Provider::GitHub, "git@github.com:o/r.git", "fix#12+c++", "main"),
              "https://github.com/o/r/compare/main...fix%2312%2Bc%2B%2B?expand=1");
}

TEST(NewPullRequestUrl, NothingToGuessForAnUnknownHost) {
    EXPECT_FALSE(newPullRequestUrl(Provider::Other, "git@git.studio.io:o/r.git", "x", "main"));
    EXPECT_FALSE(newPullRequestUrl(Provider::GitHub, "/srv/git/r.git", "x", "main"));
}
