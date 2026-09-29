#include "core/git/CredentialSources.h"

#include <gtest/gtest.h>

using namespace gity::git;

TEST(CredentialSources, AStoreFileListsHostsAndUsersButNoPasswords) {
    const auto entries = parseCredentialStore(
        "https://alice:tok%3Den@github.com\n"
        "\n"
        "https://bob%40corp:pw@git.example.com:8443/team/repo.git\n"
        "not a url\n");
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].protocol, "https");
    EXPECT_EQ(entries[0].host, "github.com");
    EXPECT_EQ(entries[0].username, "alice");
    EXPECT_TRUE(entries[0].hasPassword);
    EXPECT_EQ(entries[1].username, "bob@corp") << "percent-decoded";
    EXPECT_EQ(entries[1].host, "git.example.com:8443");
    EXPECT_EQ(entries[1].path, "team/repo.git");
}

TEST(CredentialSources, GhStatusListsEveryAccountAndWhichIsActive) {
    // gh 2.6x output, two accounts on one host, token line masked by gh.
    const auto accounts = parseGhAuthStatus(
        "github.com\n"
        "  ✓ Logged in to github.com account alex-personal (keyring)\n"
        "  - Active account: true\n"
        "  - Git operations protocol: https\n"
        "  - Token: gho_************************************\n"
        "\n"
        "  ✓ Logged in to github.com account work-account (/home/me/.config/gh/hosts.yml)\n"
        "  - Active account: false\n");
    ASSERT_EQ(accounts.size(), 2u);
    EXPECT_EQ(accounts[0].host, "github.com");
    EXPECT_EQ(accounts[0].account, "alex-personal");
    EXPECT_EQ(accounts[0].storage, "keyring");
    EXPECT_TRUE(accounts[0].active);
    EXPECT_EQ(accounts[1].account, "work-account");
    EXPECT_EQ(accounts[1].storage, "/home/me/.config/gh/hosts.yml");
    EXPECT_FALSE(accounts[1].active);
}

TEST(CredentialSources, NothingParsesToNothing) {
    EXPECT_TRUE(parseCredentialStore("").empty());
    EXPECT_TRUE(parseGhAuthStatus("You are not logged into any GitHub hosts.").empty());
}

TEST(CredentialSources, ARemoteNamingANonGhUserIsCaught) {
    // A common case: the URL names "alexdoe", GitHub CLI is signed
    // in as "alex-work". gh answers nothing; git asks for a
    // password; GitHub refuses it.
    const std::vector<GhAccount> accounts{{"github.com", "alex-work", "file", true},
                                          {"github.com", "alex-personal", "keyring", false}};
    const auto unknown = findAccountMismatch(
        "https://alexdoe@github.com/acme-games/space-game.git", accounts);
    EXPECT_EQ(unknown.kind, AccountMismatch::Kind::UnknownUser);
    EXPECT_EQ(unknown.urlUser, "alexdoe");
    EXPECT_EQ(unknown.activeAccount, "alex-work");

    const auto inactive = findAccountMismatch("https://alex-personal@github.com/me/repo.git", accounts);
    EXPECT_EQ(inactive.kind, AccountMismatch::Kind::InactiveUser);

    EXPECT_EQ(findAccountMismatch("https://github.com/me/repo.git", accounts).kind,
              AccountMismatch::Kind::None) << "no user named: the active account answers";
    EXPECT_EQ(findAccountMismatch("https://alex-work@github.com/x/y.git", accounts).kind,
              AccountMismatch::Kind::None);
    EXPECT_EQ(findAccountMismatch("https://someone@gitlab.com/x/y.git", accounts).kind,
              AccountMismatch::Kind::None) << "gh does not answer for other hosts";
    EXPECT_EQ(findAccountMismatch("git@github.com:x/y.git", accounts).kind,
              AccountMismatch::Kind::None) << "ssh";
}
