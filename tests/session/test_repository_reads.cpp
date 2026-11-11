// The libgit2 readers — status, refs, a commit's detail, submodules — against
// repositories built by git itself, in the states that are hard to fake: a
// conflict, an unborn branch, a detached HEAD, an upstream deleted on the
// remote, a submodule moved away from what its parent records.
#include "TempRepo.h"

#include "core/git/CommitDetail.h"
#include "core/git/Refs.h"
#include "core/git/Submodules.h"
#include "core/git/WorkingCopy.h"

#include <gtest/gtest.h>

using gity::git::FileState;
using gity::test::TempRepo;

namespace {

const gity::git::StatusEntry* entryFor(const gity::git::WorkingCopyStatus& status,
                                       const char* path) {
    return status.find(path);
}

git_oid oidOf(const QString& hex) {
    git_oid oid{};
    EXPECT_EQ(git_oid_fromstr(&oid, hex.toLatin1().constData()), 0);
    return oid;
}

} // namespace

TEST(Status, ModifiedStagedAndUntrackedAreToldApart) {
    TempRepo repo;
    repo.write("kept.txt", "one\n");
    repo.write("staged.txt", "one\n");
    repo.commitAll();
    repo.write("kept.txt", "two\n");
    repo.write("staged.txt", "two\n");
    repo.git({"add", "staged.txt"});
    repo.write("new.txt", "fresh\n");

    const auto status = gity::git::loadStatus(repo.repository());
    ASSERT_NE(entryFor(status, "kept.txt"), nullptr);
    EXPECT_EQ(entryFor(status, "kept.txt")->unstaged, FileState::Modified);
    EXPECT_FALSE(entryFor(status, "kept.txt")->hasStaged());
    ASSERT_NE(entryFor(status, "staged.txt"), nullptr);
    EXPECT_EQ(entryFor(status, "staged.txt")->staged, FileState::Modified);
    ASSERT_NE(entryFor(status, "new.txt"), nullptr);
    EXPECT_TRUE(entryFor(status, "new.txt")->isUntracked());
}

TEST(Status, AStagedRenameIsOneEntry) {
    TempRepo repo;
    repo.write("old.txt", "a long enough body for rename detection to see it\n");
    repo.commitAll();
    repo.git({"mv", "old.txt", "new.txt"});

    const auto status = gity::git::loadStatus(repo.repository());
    const auto* entry = entryFor(status, "new.txt");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->staged, FileState::Renamed);
    EXPECT_EQ(entry->oldPath, "old.txt");
    EXPECT_EQ(entryFor(status, "old.txt"), nullptr);
}

TEST(Status, AConflictIsReported) {
    TempRepo repo;
    repo.write("f.txt", "base\n");
    repo.commitAll();
    repo.git({"switch", "-q", "-c", "side"});
    repo.write("f.txt", "side\n");
    repo.commitAll("side");
    repo.git({"switch", "-q", "main"});
    repo.write("f.txt", "main\n");
    repo.commitAll("main");
    static_cast<void>(gity::session::GitProcess::run(repo.dir(), {"merge", "side"}));

    const auto status = gity::git::loadStatus(repo.repository());
    ASSERT_NE(entryFor(status, "f.txt"), nullptr);
    EXPECT_TRUE(entryFor(status, "f.txt")->conflicted);
    EXPECT_EQ(status.conflictedCount(), 1u);
}

TEST(Status, AnUnbornBranchHasStatusToo) {
    TempRepo repo;
    repo.write("first.txt", "one\n");
    repo.git({"add", "first.txt"});

    const auto status = gity::git::loadStatus(repo.repository());
    ASSERT_NE(entryFor(status, "first.txt"), nullptr);
    EXPECT_EQ(entryFor(status, "first.txt")->staged, FileState::Added);
}

TEST(Refs, AnUnbornBranchIsSaidToBeOne) {
    TempRepo repo;
    const auto refs = gity::git::loadRefs(repo.repository());
    EXPECT_TRUE(refs.headUnborn);
    EXPECT_TRUE(refs.localBranches.empty());
}

TEST(Refs, ADetachedHeadIsSaidToBeOne) {
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();
    repo.git({"switch", "-q", "--detach", "HEAD"});

    const auto refs = gity::git::loadRefs(repo.repository());
    EXPECT_TRUE(refs.headDetached);
    for (const auto& branch : refs.localBranches) {
        EXPECT_FALSE(branch.isHead) << branch.name;
    }
}

TEST(Refs, AnUpstreamDeletedOnTheRemoteIsFlagged) {
    TempRepo remote;
    remote.write("a.txt", "one\n");
    remote.commitAll();
    remote.git({"branch", "feature"});

    TempRepo local;
    local.git({"remote", "add", "origin", remote.dir()});
    local.git({"fetch", "-q", "origin"});
    local.git({"switch", "-q", "-c", "feature", "--track", "origin/feature"});
    remote.git({"branch", "-D", "feature"});
    local.git({"fetch", "-q", "--prune", "origin"});

    const auto refs = gity::git::loadRefs(local.repository());
    bool found = false;
    for (const auto& branch : refs.localBranches) {
        if (branch.name == "feature") {
            found = true;
            EXPECT_TRUE(branch.upstreamGone);
        }
    }
    EXPECT_TRUE(found);
}

TEST(CommitDetail, FilesAndBodyOfACommit) {
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();
    repo.write("a.txt", "two\n");
    repo.write("b.txt", "new\n");
    repo.git({"add", "-A"});
    repo.git({"commit", "-q", "-m", "Subject\n\nThe body.\n\nCo-Authored-By: Someone <s@x>"});

    const auto detail = gity::git::loadCommitDetail(repo.repository(), oidOf(repo.head()));
    EXPECT_EQ(detail.summary, "Subject");
    EXPECT_NE(detail.body.find("The body."), std::string::npos);
    EXPECT_NE(detail.body.find("Co-Authored-By"), std::string::npos) << "trailers are kept";
    ASSERT_EQ(detail.files.size(), 2u);
    EXPECT_FALSE(detail.isRoot());
}

TEST(CommitDetail, ACrLfMessageStillHasABody) {
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();
    repo.write("a.txt", "two\n");
    repo.git({"add", "-A"});
    repo.write("message.txt", "Subject\r\n\r\nA body written on Windows.\r\n");
    repo.git({"commit", "-q", "--cleanup=verbatim", "-F", "message.txt"});

    const auto detail = gity::git::loadCommitDetail(repo.repository(), oidOf(repo.head()));
    EXPECT_NE(detail.body.find("A body written on Windows."), std::string::npos) << detail.body;
}

TEST(Submodules, OneMovedAwayFromWhatItsParentRecordsIsFlagged) {
    TempRepo child;
    child.write("lib.txt", "v1\n");
    child.commitAll();

    TempRepo parent;
    parent.write("app.txt", "app\n");
    parent.commitAll();
    parent.git({"-c", "protocol.file.allow=always", "submodule", "add", "-q", child.dir(),
                "vendor/lib"});
    parent.commitAll("add the submodule");

    auto entries = gity::git::listSubmodules(parent.repository());
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].path, "vendor/lib");
    EXPECT_TRUE(entries[0].initialised);
    EXPECT_FALSE(entries[0].needsAttention());

    const QString inside = parent.dir() + QStringLiteral("/vendor/lib");
    QFile file(inside + QStringLiteral("/lib.txt"));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("v2\n");
    file.close();
    // The submodule is a clone with a config of its own, so it has no
    // identity; the tests shut out the global one.
    ASSERT_TRUE(gity::session::GitProcess::run(
                    inside, {"-c", "user.name=Test", "-c", "user.email=test@invalid", "commit",
                             "-qam", "moved"})
                    .ok());

    entries = gity::git::listSubmodules(parent.repository());
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_TRUE(entries[0].movedAway);
}
