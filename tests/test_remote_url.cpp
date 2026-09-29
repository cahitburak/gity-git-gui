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
                  "https://alex.doe@example.com@github.com/acme-games/shared.git",
                  "alex-work"),
              "https://alex-work@github.com/acme-games/shared.git");
}
