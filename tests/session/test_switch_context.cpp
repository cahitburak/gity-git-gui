// What a switch involves, from the refs and status already loaded: the
// questions Checkout and New Branch ask depend on it.
#include "session/SwitchContext.h"

#include <gtest/gtest.h>

using gity::session::RefKind;
using gity::session::switchContext;

namespace {

git_oid oid(char fill) {
    git_oid id{};
    const std::string hex(GIT_OID_SHA1_HEXSIZE, fill);
    git_oid_fromstr(&id, hex.c_str());
    return id;
}

gity::git::RefEntry branch(const std::string& name, char at, bool head = false) {
    gity::git::RefEntry entry;
    entry.name = name;
    entry.target = oid(at);
    entry.isHead = head;
    return entry;
}

gity::git::RefSet refs() {
    gity::git::RefSet set;
    set.localBranches = {branch("main", 'a', true), branch("feature", 'b'), branch("level", 'a')};
    set.remoteBranches = {branch("origin/feature", 'c')};
    gity::git::SubmoduleEntry lib;
    lib.path = "lib";
    lib.initialised = true;
    gity::git::SubmoduleEntry absent;
    absent.path = "docs";
    absent.initialised = false;
    set.submodules = {lib, absent};
    return set;
}

gity::git::StatusEntry changed(const std::string& path) {
    gity::git::StatusEntry entry;
    entry.path = path;
    entry.unstaged = gity::git::FileState::Modified;
    return entry;
}

} // namespace

TEST(SwitchContext, NamesWhatKindOfRefItIs) {
    const auto set = refs();
    EXPECT_EQ(switchContext(&set, nullptr, QStringLiteral("feature")).kind, RefKind::Local);
    EXPECT_EQ(switchContext(&set, nullptr, QStringLiteral("origin/feature")).kind, RefKind::Remote);
    // A tag or a commit id: checked out detached.
    EXPECT_EQ(switchContext(&set, nullptr, QStringLiteral("v1.0")).kind, RefKind::Detached);
}

TEST(SwitchContext, AtTheCommitAlreadyCheckedOutNothingMovesOnDisk) {
    const auto set = refs();
    EXPECT_TRUE(switchContext(&set, nullptr, QStringLiteral("level")).sameCommit);
    EXPECT_TRUE(switchContext(&set, nullptr, QString()).sameCommit) << "empty means HEAD";
    EXPECT_FALSE(switchContext(&set, nullptr, QStringLiteral("feature")).sameCommit);
    EXPECT_FALSE(switchContext(&set, nullptr, QStringLiteral("v1.0")).sameCommit);
}

TEST(SwitchContext, CountsTheChangesThatAreOnlySubmodules) {
    const auto set = refs();
    gity::git::WorkingCopyStatus status;
    status.entries = {changed("lib"), changed("docs"), changed("README.md")};
    const auto context = switchContext(&set, &status, QStringLiteral("feature"));
    EXPECT_TRUE(context.hasSubmodules);
    // "docs" is a submodule nobody initialised, so a change there is not one
    // that updating submodules would put right.
    EXPECT_EQ(context.submoduleChanges, 1);
}

TEST(SwitchContext, NothingLoadedYetAsksNothingOfSubmodules) {
    const auto context = switchContext(nullptr, nullptr, QStringLiteral("main"));
    EXPECT_EQ(context.kind, RefKind::Detached);
    EXPECT_FALSE(context.hasSubmodules);
    EXPECT_EQ(context.submoduleChanges, 0);
    EXPECT_FALSE(context.sameCommit);
}
