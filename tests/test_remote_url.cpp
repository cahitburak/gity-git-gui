#include "core/git/RemoteUrl.h"

#include <gtest/gtest.h>

using gity::git::folderNameForRemote;

TEST(FolderNameForRemote, HttpsWithAndWithoutTheGitSuffix) {
    EXPECT_EQ(folderNameForRemote("https://github.com/alexdoe/Gity.git"), "Gity");
    EXPECT_EQ(folderNameForRemote("https://github.com/alexdoe/Gity"), "Gity");
}

TEST(FolderNameForRemote, ScpStyleSshHasNoSchemeAndAColonWhereThePathStarts) {
    // The case a naive "text after the last slash" rule still gets right, and
    // the one below is the case it does not.
    EXPECT_EQ(folderNameForRemote("git@github.com:alexdoe/Gity.git"), "Gity");
    EXPECT_EQ(folderNameForRemote("git@github.com:Gity.git"), "Gity");
}

TEST(FolderNameForRemote, APortIsNotMistakenForAPathSeparator) {
    // The colon here is a port, and the last path separator is still the
    // slash — taking the later of the two is what makes this work.
    EXPECT_EQ(folderNameForRemote("ssh://git@host:2222/team/project.git"), "project");
}

TEST(FolderNameForRemote, TrailingSlashesDoNotProduceAnEmptyName) {
    EXPECT_EQ(folderNameForRemote("https://github.com/owner/project.git/"), "project");
    EXPECT_EQ(folderNameForRemote("https://github.com/owner/project//"), "project");
}

TEST(FolderNameForRemote, LocalPathsAreRemotesToo) {
    EXPECT_EQ(folderNameForRemote("/srv/git/project.git"), "project");
    EXPECT_EQ(folderNameForRemote("file:///srv/git/project.git"), "project");
}

TEST(FolderNameForRemote, PastedWhitespaceIsIgnored) {
    EXPECT_EQ(folderNameForRemote("  https://github.com/owner/project.git  "), "project");
}

TEST(FolderNameForRemote, NothingToTakeYieldsNothing) {
    EXPECT_EQ(folderNameForRemote(""), "");
    EXPECT_EQ(folderNameForRemote("   "), "");
    EXPECT_EQ(folderNameForRemote("///"), "");
}

TEST(FolderNameForRemote, ARepositoryLiterallyNamedDotGitKeepsItsName) {
    // Chopping the suffix unconditionally would leave an empty folder name and
    // a clone into the parent directory itself.
    EXPECT_EQ(folderNameForRemote("https://host/owner/.git"), ".git");
}

TEST(RemoteUrlWithUser, AddsTheAccountToAnHttpsUrl) {
    // The fix for a work repository reached with personal credentials: git
    // keys credentials by host *and* username, so naming the account in the
    // URL is what makes the right one be asked for.
    EXPECT_EQ(gity::git::remoteUrlWithUser("https://github.com/Org/repo.git", "work-account"),
              "https://work-account@github.com/Org/repo.git");
}

TEST(RemoteUrlWithUser, ReplacesAUserinfoThatIsAlreadyThere) {
    EXPECT_EQ(gity::git::remoteUrlWithUser("https://personal@github.com/Org/repo.git", "work"),
              "https://work@github.com/Org/repo.git");
}

TEST(RemoteUrlWithUser, AnAtSignInThePathIsNotUserinfo) {
    // Taking the first '@' anywhere would mangle the path into the host.
    EXPECT_EQ(gity::git::remoteUrlWithUser("https://host/team/name@version.git", "me"),
              "https://me@host/team/name@version.git");
}

TEST(RemoteUrlWithUser, LeavesScpStyleAlone) {
    // "git@" there is the ssh user, not an account name. Rewriting it stops
    // the remote resolving at all.
    EXPECT_EQ(gity::git::remoteUrlWithUser("git@github.com:Org/repo.git", "work"),
              "git@github.com:Org/repo.git");
}

TEST(RemoteUrlWithUser, LeavesAlonePathsAndEmptyNames) {
    EXPECT_EQ(gity::git::remoteUrlWithUser("/srv/git/repo.git", "work"), "/srv/git/repo.git");
    EXPECT_EQ(gity::git::remoteUrlWithUser("https://github.com/a/b.git", ""),
              "https://github.com/a/b.git");
}

TEST(RemoteUrl, AnEmailUsernameIsEscaped) {
    // Written bare, the '@' of an email split the address in the wrong place:
    // https://alex.doe@example.com@github.com/... — found in a real
    // submodule this function had configured.
    EXPECT_EQ(gity::git::remoteUrlWithUser("https://github.com/acme-games/shared.git",
                                           "alex.doe@example.com"),
              "https://alex.doe%40example.com@github.com/acme-games/shared.git");
}

TEST(RemoteUrl, AnAlreadyBrokenAddressIsRepaired) {
    EXPECT_EQ(gity::git::remoteUrlWithUser(
                  "https://alex.doe@example.com@github.com/acme-games/shared.git", "alex-work"),
              "https://alex-work@github.com/acme-games/shared.git");
}

TEST(MaskUrlCredentials, HidesAPassword) {
    EXPECT_EQ(gity::git::maskUrlCredentials("https://me:s3cret@github.com/o/r.git"),
              "https://me:***@github.com/o/r.git");
}

TEST(MaskUrlCredentials, HidesATokenUsedAsTheUsername) {
    EXPECT_EQ(gity::git::maskUrlCredentials("https://ghp_abcdef0123456789@github.com/o/r"),
              "https://***@github.com/o/r");
    EXPECT_EQ(
        gity::git::maskUrlCredentials("https://glpat-xxxxxxxxxxxxxxxxxxxx@gitlab.com/o/r.git"),
        "https://***@gitlab.com/o/r.git");
    const std::string longToken(40, 'a');
    EXPECT_EQ(gity::git::maskUrlCredentials("https://" + longToken + "@host/r"),
              "https://***@host/r");
}

TEST(MaskUrlCredentials, KeepsAPlainUsername) {
    EXPECT_EQ(gity::git::maskUrlCredentials("https://alice@github.com/o/r"),
              "https://alice@github.com/o/r");
    EXPECT_EQ(gity::git::maskUrlCredentials("git@github.com:o/r.git"), "git@github.com:o/r.git");
}

TEST(MaskUrlCredentials, MasksEveryUrlInAMessage) {
    EXPECT_EQ(
        gity::git::maskUrlCredentials("fatal: unable to access 'https://me:pw@a.com/r/': 403\n"
                                      "hint: or 'https://you:pw2@b.com/x'"),
        "fatal: unable to access 'https://me:***@a.com/r/': 403\n"
        "hint: or 'https://you:***@b.com/x'");
}

TEST(MaskUrlCredentials, LeavesTextWithoutUrlsAlone) {
    EXPECT_EQ(gity::git::maskUrlCredentials("nothing to see: a@b"), "nothing to see: a@b");
    EXPECT_EQ(gity::git::maskUrlCredentials(""), "");
}

TEST(UnmaskUrlCredentials, PutsThePasswordBackAfterAnEdit) {
    EXPECT_EQ(gity::git::unmaskUrlCredentials("https://me:***@github.com/o/renamed.git",
                                              "https://me:s3cret@github.com/o/r.git"),
              "https://me:s3cret@github.com/o/renamed.git");
}

TEST(MaskUrlCredentials, HidesATokenUsernameThatHasAPasswordAfterIt) {
    // GitHub documents a token as the user with a filler password, and some
    // tools write it with an empty one. Keeping "the username" kept the token.
    EXPECT_EQ(gity::git::maskUrlCredentials("https://ghp_abcdef0123456789:@github.com/o/r"),
              "https://***:***@github.com/o/r");
    EXPECT_EQ(
        gity::git::maskUrlCredentials("https://ghp_abcdef0123456789:x-oauth-basic@github.com/o/r"),
        "https://***:***@github.com/o/r");
    // A plain name before a password is still shown.
    EXPECT_EQ(gity::git::maskUrlCredentials("https://alice:pw@github.com/o/r"),
              "https://alice:***@github.com/o/r");
}

TEST(UnmaskUrlCredentials, PutsTheTokenBack) {
    EXPECT_EQ(gity::git::unmaskUrlCredentials("https://***@github.com/o/renamed",
                                              "https://ghp_abcdef0123456789@github.com/o/r"),
              "https://ghp_abcdef0123456789@github.com/o/renamed");
}

TEST(UnmaskUrlCredentials, PutsBackATokenUsernameAndItsPassword) {
    EXPECT_EQ(gity::git::unmaskUrlCredentials(
                  "https://***:***@github.com/o/renamed",
                  "https://ghp_abcdef0123456789:x-oauth-basic@github.com/o/r"),
              "https://ghp_abcdef0123456789:x-oauth-basic@github.com/o/renamed");
}

TEST(UnmaskUrlCredentials, ASecretDoesNotFollowTheRemoteToAnotherHost) {
    // Restoring it would send a GitHub token to whatever server the edit
    // named. The masked parts are dropped instead, and no literal "***" is
    // saved either.
    EXPECT_EQ(gity::git::unmaskUrlCredentials("https://***@gitlab.com/o/r",
                                              "https://ghp_abcdef0123456789@github.com/o/r"),
              "https://gitlab.com/o/r");
    EXPECT_EQ(gity::git::unmaskUrlCredentials("https://me:***@evil.example/o/r",
                                              "https://me:s3cret@github.com/o/r"),
              "https://me@evil.example/o/r");
    EXPECT_EQ(gity::git::unmaskUrlCredentials("https://me:***@github.com:8443/o/r",
                                              "https://me:s3cret@github.com/o/r"),
              "https://me@github.com:8443/o/r")
        << "another port is another server";
}

TEST(UnmaskUrlCredentials, NorIsItSentInTheClear) {
    EXPECT_EQ(gity::git::unmaskUrlCredentials("http://me:***@github.com/o/r",
                                              "https://me:s3cret@github.com/o/r"),
              "http://me@github.com/o/r");
    // Upgrading to https is the safe direction, and keeps it.
    EXPECT_EQ(
        gity::git::unmaskUrlCredentials("https://me:***@host/o/r", "http://me:s3cret@host/o/r"),
        "https://me:s3cret@host/o/r");
    // Host names are not case-sensitive.
    EXPECT_EQ(gity::git::unmaskUrlCredentials("https://me:***@GitHub.com/o/r",
                                              "https://me:s3cret@github.com/o/r"),
              "https://me:s3cret@GitHub.com/o/r");
}

TEST(UnmaskUrlCredentials, AnythingTypedByHandWins) {
    EXPECT_EQ(gity::git::unmaskUrlCredentials("https://me:new@github.com/o/r",
                                              "https://me:old@github.com/o/r"),
              "https://me:new@github.com/o/r");
    EXPECT_EQ(
        gity::git::unmaskUrlCredentials("https://github.com/o/r", "https://me:old@github.com/o/r"),
        "https://github.com/o/r");
}

TEST(FolderNameForRemote, AWindowsPath) {
    EXPECT_EQ(folderNameForRemote("C:\\src\\proj.git"), "proj");
    EXPECT_EQ(folderNameForRemote("C:\\src\\proj\\"), "proj");
}
