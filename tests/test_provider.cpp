#include "core/git/Provider.h"

#include <gtest/gtest.h>

using gity::git::hostOfRemote;
using gity::git::Provider;
using gity::git::providerForHost;

TEST(HostOfRemote, TheShapesARemoteActuallyArrivesIn) {
    EXPECT_EQ(hostOfRemote("https://github.com/owner/repo.git"), "github.com");
    EXPECT_EQ(hostOfRemote("git@github.com:owner/repo.git"), "github.com");
    EXPECT_EQ(hostOfRemote("ssh://git@gitlab.example.com:2222/team/repo.git"),
              "gitlab.example.com");
    EXPECT_EQ(hostOfRemote("https://user@github.com/owner/repo.git"), "github.com");
}

TEST(HostOfRemote, CredentialsInAUrlDoNotBecomeTheHost) {
    // "user:token@host" appears in CI configuration and pasted URLs. Taking
    // the text before the slash would make the token part of the host.
    EXPECT_EQ(hostOfRemote("https://user:ghp_secret@github.com/owner/repo.git"), "github.com");
}

TEST(HostOfRemote, ALocalPathHasNoHost) {
    EXPECT_TRUE(hostOfRemote("/srv/git/project.git").empty());
    EXPECT_TRUE(hostOfRemote("../sibling/project.git").empty());
    EXPECT_TRUE(hostOfRemote("file:///srv/git/project.git").empty());
    EXPECT_TRUE(hostOfRemote("C:/repos/project.git").empty());
    EXPECT_TRUE(hostOfRemote("").empty());
}

TEST(HostOfRemote, IsCaseInsensitiveAndIgnoresPastedWhitespace) {
    EXPECT_EQ(hostOfRemote("  https://GitHub.COM/owner/repo.git  "), "github.com");
}

TEST(ProviderForHost, ThePublicInstances) {
    EXPECT_EQ(providerForHost("github.com"), Provider::GitHub);
    EXPECT_EQ(providerForHost("gitlab.com"), Provider::GitLab);
    EXPECT_EQ(providerForHost("bitbucket.org"), Provider::Bitbucket);
    EXPECT_EQ(providerForHost("dev.azure.com"), Provider::AzureDevOps);
    EXPECT_EQ(providerForHost("myorg.visualstudio.com"), Provider::AzureDevOps);
}

TEST(ProviderForHost, ALookalikeHostIsNotTheRealOne) {
    // A substring search would call all of these GitHub. Getting this wrong
    // would put a trusted name on an untrusted host.
    EXPECT_EQ(providerForHost("github.com.evil.example"), Provider::Other);
    EXPECT_EQ(providerForHost("notgithub.com"), Provider::Other);
    EXPECT_EQ(providerForHost("mygithub.com"), Provider::Other);
}

TEST(ProviderForHost, SelfHostedIsRecognisedOnlyWhenTheNameSaysWhich) {
    EXPECT_EQ(providerForHost("gitlab.studio.example"), Provider::GitLab);
    EXPECT_EQ(providerForHost("github.studio.example"), Provider::GitHub);
    // A studio's "git.example.com" could be any of them, and a confident wrong
    // label is worse than admitting it is unknown.
    EXPECT_EQ(providerForHost("git.studio.example"), Provider::Other);
    EXPECT_EQ(providerForHost("code.studio.example"), Provider::Other);
}

TEST(ProviderForHost, NothingIsOther) {
    EXPECT_EQ(providerForHost(""), Provider::Other);
}

TEST(Provider, AnUnescapedEmailUserDoesNotBecomeTheHost) {
    EXPECT_EQ(gity::git::hostOfRemote(
                  "https://alex.doe@example.com@github.com/acme-games/shared.git"),
              "github.com");
    EXPECT_EQ(gity::git::hostOfRemote("git@github.com:owner/repo.git"), "github.com");
}
