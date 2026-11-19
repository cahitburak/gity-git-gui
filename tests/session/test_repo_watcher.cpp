// Changes made outside Gity, noticed: against a real repository, with git
// doing the changing as a terminal would.
#include "TempRepo.h"

#include "session/RepoWatcher.h"

#include <gtest/gtest.h>

#include <QEventLoop>
#include <QTimer>

using gity::session::RepoWatcher;
using gity::test::TempRepo;

namespace {

/// Runs the event loop until the watcher reports or `timeoutMs` passes, and
/// returns what it reported (0 for nothing).
int waitForChange(RepoWatcher& watcher, int timeoutMs = 3000) {
    int kinds = 0;
    QEventLoop loop;
    const auto connection =
        QObject::connect(&watcher, &RepoWatcher::changed, &loop, [&](int reported) {
            kinds = reported;
            loop.quit();
        });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    QObject::disconnect(connection);
    return kinds;
}

/// Lets queued file-system notifications arrive without reporting them.
void settle(int ms = 100) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

std::unique_ptr<RepoWatcher> watch(const TempRepo& repo) {
    const QString gitDir = repo.dir() + QStringLiteral("/.git");
    return std::make_unique<RepoWatcher>(repo.dir(), gitDir, gitDir,
                                         [](const QString& relative, bool) {
                                             return relative == QStringLiteral("build") ||
                                                    relative.startsWith(QStringLiteral("build/"));
                                         });
}

} // namespace

TEST(RepoWatcher, ACommitInATerminalIsAChangeOfRefs) {
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();
    auto watcher = watch(repo);
    settle();

    repo.write("a.txt", "two\n");
    repo.commitAll("from a terminal");
    const int kinds = waitForChange(*watcher);
    EXPECT_NE(kinds & RepoWatcher::Refs, 0) << kinds;
}

TEST(RepoWatcher, SwitchingBranchIsAChangeOfHead) {
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();
    repo.git({"branch", "other"});
    auto watcher = watch(repo);
    settle();

    repo.git({"switch", "-q", "other"});
    const int kinds = waitForChange(*watcher);
    EXPECT_NE(kinds & RepoWatcher::Head, 0) << kinds;
}

TEST(RepoWatcher, StagingInATerminalIsAChangeOfTheIndex) {
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();
    repo.write("a.txt", "two\n");
    auto watcher = watch(repo);
    settle();
    watcher->absorb();

    repo.git({"add", "a.txt"});
    const int kinds = waitForChange(*watcher);
    EXPECT_NE(kinds & RepoWatcher::Index, 0) << kinds;
}

TEST(RepoWatcher, AnEditInTheWorkingTreeIsNoticed) {
    TempRepo repo;
    repo.write("src/a.txt", "one\n");
    repo.commitAll();
    auto watcher = watch(repo);
    if (!watcher->watchesWorktree()) {
        GTEST_SKIP() << "the working tree is watched natively on Linux only so far";
    }
    settle();

    repo.write("src/a.txt", "edited in an editor\n");
    EXPECT_NE(waitForChange(*watcher) & RepoWatcher::Worktree, 0);
}

TEST(RepoWatcher, AFileInANewDirectoryIsNoticed) {
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();
    auto watcher = watch(repo);
    if (!watcher->watchesWorktree()) {
        GTEST_SKIP() << "the working tree is watched natively on Linux only so far";
    }
    settle();

    QDir(repo.dir()).mkpath(QStringLiteral("fresh/deeper"));
    static_cast<void>(waitForChange(*watcher, 600)); // the directories themselves
    repo.write("fresh/deeper/b.txt", "new\n");
    EXPECT_NE(waitForChange(*watcher) & RepoWatcher::Worktree, 0);
}

TEST(RepoWatcher, IgnoredDirectoriesAreNotWatched) {
    // Build output is often most of the tree, and rewritten on every build.
    TempRepo repo;
    repo.write(".gitignore", "build/\n");
    repo.write("build/out.o", "x");
    repo.commitAll();
    auto watcher = watch(repo);
    if (!watcher->watchesWorktree()) {
        GTEST_SKIP() << "the working tree is watched natively on Linux only so far";
    }
    settle();

    repo.write("build/out.o", "rebuilt");
    EXPECT_EQ(waitForChange(*watcher, 800), 0);
}

TEST(RepoWatcher, GitysOwnSnapshotsAreNotAChange) {
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();
    auto watcher = watch(repo);
    settle();

    repo.git({"update-ref", "refs/gity/snapshots", repo.head()});
    EXPECT_EQ(waitForChange(*watcher, 800), 0);
}

TEST(RepoWatcher, ARepositoryInAFolderNamedLikeGitysRefsStillSeesItsBranches) {
    // Gity's own refs were recognised by "/refs/gity" anywhere in the path,
    // so a repository under ~/refs/gity-work had every branch ignored.
    QTemporaryDir base;
    ASSERT_TRUE(base.isValid());
    const QString dir = base.path() + QStringLiteral("/refs/gity-work/repo");
    ASSERT_TRUE(QDir().mkpath(dir));
    const auto git = [&dir](const QStringList& args) {
        const auto result = gity::session::GitProcess::run(dir, args);
        EXPECT_TRUE(result.ok()) << result.errorOutput.toStdString();
    };
    git({"init", "-q", "-b", "main"});
    git({"-c", "user.name=T", "-c", "user.email=t@invalid", "commit", "-q", "--allow-empty", "-m",
         "base"});

    RepoWatcher watcher(dir, dir + QStringLiteral("/.git"), dir + QStringLiteral("/.git"), {});
    settle();
    git({"branch", "other"});
    EXPECT_NE(waitForChange(watcher) & RepoWatcher::Refs, 0);
}

TEST(RepoWatcher, AFolderMovedOutOfTheTreeIsNoLongerWatched) {
    TempRepo repo;
    repo.write("away/a.txt", "one\n");
    repo.commitAll();
    QTemporaryDir elsewhere;
    ASSERT_TRUE(elsewhere.isValid());
    auto watcher = watch(repo);
    if (!watcher->watchesWorktree()) {
        GTEST_SKIP() << "the working tree is watched natively on Linux only so far";
    }
    settle();

    const QString moved = elsewhere.path() + QStringLiteral("/away");
    ASSERT_TRUE(QDir().rename(repo.dir() + QStringLiteral("/away"), moved));
    static_cast<void>(waitForChange(*watcher, 600)); // the move itself
    QFile file(moved + QStringLiteral("/a.txt"));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("edited where it went\n");
    file.close();
    EXPECT_EQ(waitForChange(*watcher, 800), 0);
}

TEST(RepoWatcher, AFolderThatBecomesARepositoryIsLeftToIt) {
    // An initialised submodule is skipped when watching starts; one
    // initialised while watching was watched file by file.
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();
    QDir(repo.dir()).mkpath(QStringLiteral("sub"));
    auto watcher = watch(repo);
    if (!watcher->watchesWorktree()) {
        GTEST_SKIP() << "the working tree is watched natively on Linux only so far";
    }
    settle();

    ASSERT_TRUE(gity::session::GitProcess::run(repo.dir() + QStringLiteral("/sub"),
                                               {"init", "-q", "-b", "main"})
                    .ok());
    settle(300);
    watcher->absorb();
    repo.write("sub/inside.txt", "the submodule's own\n");
    EXPECT_EQ(waitForChange(*watcher, 800), 0);
}

TEST(RepoWatcher, AbsorbedChangesAreNotReportedAgain) {
    // Gity reads the repository itself after its own commands; what changed
    // before that read must not trigger a second one.
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();
    auto watcher = watch(repo);
    settle();

    repo.write("a.txt", "two\n");
    repo.commitAll("by Gity");
    settle(50);
    watcher->absorb();
    EXPECT_EQ(waitForChange(*watcher, 800), 0);
}
