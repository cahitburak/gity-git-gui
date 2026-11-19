// Requests through a whole session — the request on the UI side, the worker
// running git — against real repositories, for what only shows end to end:
// how a name typed into a dialog reaches git's argument list, and what a
// superseded history walk still has on its way to the window.
#include "TempRepo.h"

#include "session/RepoSession.h"

#include <gtest/gtest.h>

#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include <functional>

using gity::session::RefKind;
using gity::session::RepoSession;
using gity::test::TempRepo;

namespace {

/// Opens `repo` in `session` and waits for its history to be walked.
bool openAndWait(RepoSession& session, const TempRepo& repo) {
    bool walked = false;
    QEventLoop loop;
    const auto connection = QObject::connect(&session, &RepoSession::finished, &loop,
                                             [&](quint64, double, double, bool) {
                                                 walked = true;
                                                 loop.quit();
                                             });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    session.open(repo.dir());
    loop.exec();
    QObject::disconnect(connection);
    return walked;
}

struct Outcome {
    bool arrived = false;
    bool ok = false;
    QString message;
};

/// Runs `request` and waits for the history edit it starts to finish.
Outcome runEdit(RepoSession& session, const std::function<void()>& request) {
    Outcome outcome;
    QEventLoop loop;
    const auto connection =
        QObject::connect(&session, &RepoSession::historyEditFinished, &loop,
                         [&](const QString&, bool ok, bool, const QString& message) {
                             outcome = {true, ok, message};
                             loop.quit();
                         });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    request();
    loop.exec();
    QObject::disconnect(connection);
    return outcome;
}

bool refExists(const TempRepo& repo, const QString& ref) {
    return gity::session::GitProcess::run(repo.dir(), {"show-ref", "--verify", "--quiet", ref})
        .ok();
}

} // namespace

TEST(SessionCommands, ABranchNamedLikeAnOptionDeletesNothing) {
    // `git branch -D main` is what a new branch called "-D", started from
    // main, used to run.
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();
    repo.git({"switch", "-q", "-c", "other"});

    RepoSession session;
    ASSERT_TRUE(openAndWait(session, repo));
    const Outcome outcome = runEdit(session, [&] {
        session.requestCreateBranch(QStringLiteral("-D"), QStringLiteral("main"), RefKind::Local,
                                    false);
    });
    ASSERT_TRUE(outcome.arrived);
    EXPECT_FALSE(outcome.ok);
    EXPECT_TRUE(refExists(repo, QStringLiteral("refs/heads/main"))) << "main was deleted";
}

TEST(SessionCommands, ATagNamedLikeAnOptionDeletesNothing) {
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();
    repo.git({"tag", "v1"});

    RepoSession session;
    ASSERT_TRUE(openAndWait(session, repo));
    const Outcome outcome = runEdit(session, [&] {
        session.requestCreateTag(QStringLiteral("-d"), QStringLiteral("v1"), QString());
    });
    ASSERT_TRUE(outcome.arrived);
    EXPECT_FALSE(outcome.ok);
    EXPECT_TRUE(refExists(repo, QStringLiteral("refs/tags/v1"))) << "v1 was deleted";
}

TEST(SessionCommands, BranchesAndTagsStillCreateAndDelete) {
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();

    RepoSession session;
    ASSERT_TRUE(openAndWait(session, repo));
    EXPECT_TRUE(runEdit(session, [&] {
                    session.requestCreateBranch(QStringLiteral("topic"), QStringLiteral("main"),
                                                RefKind::Local, false);
                }).ok);
    EXPECT_TRUE(refExists(repo, QStringLiteral("refs/heads/topic")));
    EXPECT_TRUE(
        runEdit(session, [&] { session.requestDeleteBranch(QStringLiteral("topic"), false); }).ok);
    EXPECT_FALSE(refExists(repo, QStringLiteral("refs/heads/topic")));

    EXPECT_TRUE(runEdit(session, [&] {
                    session.requestCreateTag(QStringLiteral("v2"), QStringLiteral("main"),
                                             QStringLiteral("release"));
                }).ok);
    EXPECT_TRUE(refExists(repo, QStringLiteral("refs/tags/v2")));
    EXPECT_TRUE(runEdit(session, [&] { session.requestDeleteTag(QStringLiteral("v2")); }).ok);
    EXPECT_FALSE(refExists(repo, QStringLiteral("refs/tags/v2")));
}

TEST(SessionCommands, DiscardingBeforeTheFirstCommitRemovesWhatWasStaged) {
    // `restore --source=HEAD` has no HEAD to read before the first commit,
    // and refused every staged file.
    TempRepo repo;
    repo.write("staged.txt", "new\n");
    repo.git({"add", "staged.txt"});
    repo.write("loose.txt", "untracked\n");

    RepoSession session;
    // There is no history to walk yet, so no walk to wait for: the discard
    // is queued behind the open, which is enough.
    session.open(repo.dir());
    const Outcome outcome = runEdit(session, [&] {
        session.requestDiscardPaths({QStringLiteral("staged.txt"), QStringLiteral("loose.txt")});
    });
    ASSERT_TRUE(outcome.arrived);
    EXPECT_TRUE(outcome.ok) << outcome.message.toStdString();
    EXPECT_FALSE(QFileInfo::exists(repo.dir() + QStringLiteral("/staged.txt")));
    EXPECT_FALSE(QFileInfo::exists(repo.dir() + QStringLiteral("/loose.txt")));
    EXPECT_FALSE(repo.inIndex(QStringLiteral("staged.txt")));
}

TEST(SessionCommands, DiscardingANewStagedFileAfterTheFirstCommitStillWorks) {
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();
    repo.write("a.txt", "two\n");
    repo.write("staged.txt", "new\n");
    repo.git({"add", "staged.txt"});

    RepoSession session;
    ASSERT_TRUE(openAndWait(session, repo));
    const Outcome outcome = runEdit(session, [&] {
        session.requestDiscardPaths({QStringLiteral("a.txt"), QStringLiteral("staged.txt")});
    });
    ASSERT_TRUE(outcome.arrived);
    EXPECT_TRUE(outcome.ok) << outcome.message.toStdString();
    EXPECT_EQ(repo.read("a.txt"), "one\n");
    EXPECT_FALSE(QFileInfo::exists(repo.dir() + QStringLiteral("/staged.txt")));
    EXPECT_FALSE(repo.inIndex(QStringLiteral("staged.txt")));
}

TEST(SessionCommands, CancelStopsAFetchTheWorkerHasNotReportedStartingYet) {
    // Closing a tab cancels its network command at once — before the
    // worker's "started" has come back. That used to be ignored, and the
    // fetch ran to the end.
    TempRepo remote;
    remote.write("a.txt", "one\n");
    remote.commitAll();
    TempRepo repo;
    repo.write("b.txt", "one\n");
    repo.commitAll();
    repo.git({"remote", "add", "origin", remote.dir()});
    // A remote that takes its time to answer.
    repo.git({"config", "remote.origin.uploadpack", "sleep 10; git-upload-pack"});

    RepoSession session;
    ASSERT_TRUE(openAndWait(session, repo));
    bool arrived = false;
    bool ok = true;
    QEventLoop loop;
    QObject::connect(&session, &RepoSession::networkFinished, &loop,
                     [&](const QString&, bool succeeded, const QString&, const QString&) {
                         arrived = true;
                         ok = succeeded;
                         loop.quit();
                     });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    QElapsedTimer timer;
    timer.start();
    session.requestFetch(false);
    session.cancelNetwork();
    loop.exec();

    ASSERT_TRUE(arrived);
    EXPECT_FALSE(ok);
    EXPECT_LT(timer.elapsed(), 5000) << "the fetch ran to the end";
}

TEST(SessionCommands, ASecondCancelStopsTheFetchAskedForBetweenTheTwo) {
    // Each cancel lowers the flag again once what was queued before it has
    // run. The first one's lowering ran ahead of the second fetch, and
    // undid the second cancel: that fetch ran to the end.
    TempRepo remote;
    remote.write("a.txt", "one\n");
    remote.commitAll();
    TempRepo repo;
    repo.write("b.txt", "one\n");
    repo.commitAll();
    repo.git({"remote", "add", "origin", remote.dir()});
    repo.git({"config", "remote.origin.uploadpack", "sleep 10; git-upload-pack"});

    RepoSession session;
    ASSERT_TRUE(openAndWait(session, repo));
    int finished = 0;
    QEventLoop loop;
    QObject::connect(&session, &RepoSession::networkFinished, &loop,
                     [&](const QString&, bool, const QString&, const QString&) {
                         if (++finished == 2) {
                             loop.quit();
                         }
                     });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    QElapsedTimer timer;
    timer.start();
    session.requestFetch(false);
    session.cancelNetwork();
    session.requestFetch(false);
    session.cancelNetwork();
    loop.exec();

    ASSERT_EQ(finished, 2);
    EXPECT_LT(timer.elapsed(), 8000) << "the second fetch ran to the end";
}

namespace {

gity::session::PreviewPtr previewOf(RepoSession& session, gity::session::Preview::Kind kind) {
    gity::session::PreviewPtr preview;
    QEventLoop loop;
    const auto connection = QObject::connect(&session, &RepoSession::previewReady, &loop,
                                             [&](gity::session::PreviewPtr ready) {
                                                 preview = std::move(ready);
                                                 loop.quit();
                                             });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    session.requestPreview(kind);
    loop.exec();
    QObject::disconnect(connection);
    return preview;
}

} // namespace

TEST(SessionCommands, APushPreviewDescribesTheRemoteThePushGoesTo) {
    // Fetched from upstream, pushed to a fork. The upstream has moved on;
    // the fork has not. The preview compared with the upstream, called the
    // push rejected, and offered a force push — to the upstream.
    TempRepo upstream;
    upstream.write("a.txt", "one\n");
    upstream.commitAll();
    TempRepo fork;
    fork.git({"config", "receive.denyCurrentBranch", "ignore"});
    TempRepo repo;
    repo.git({"remote", "add", "upstream", upstream.dir()});
    repo.git({"remote", "add", "fork", fork.dir()});
    repo.git({"fetch", "-q", "upstream"});
    repo.git({"reset", "-q", "--hard", "upstream/main"});
    repo.git({"branch", "-q", "--set-upstream-to=upstream/main", "main"});
    repo.git({"config", "remote.pushDefault", "fork"});
    repo.git({"push", "-q", "fork", "main"});

    upstream.write("a.txt", "two\n");
    upstream.commitAll("upstream moves on");
    repo.git({"fetch", "-q", "upstream"});
    repo.write("b.txt", "mine\n");
    repo.commitAll("my work");

    RepoSession session;
    ASSERT_TRUE(openAndWait(session, repo));
    const auto preview = previewOf(session, gity::session::Preview::Kind::Push);
    ASSERT_TRUE(preview);
    EXPECT_TRUE(preview->problem.isEmpty()) << preview->problem.toStdString();
    EXPECT_EQ(preview->remote, QStringLiteral("fork"));
    EXPECT_EQ(preview->remoteBranch, QStringLiteral("main"));
    EXPECT_EQ(preview->outgoing, 1);
    EXPECT_EQ(preview->incoming, 0);
    EXPECT_TRUE(preview->leaseOid.isEmpty()) << "a force push was offered";
}

TEST(SessionCommands, AFirstPushToAForkIsPreviewedAsPublishingThere) {
    TempRepo upstream;
    upstream.write("a.txt", "one\n");
    upstream.commitAll();
    TempRepo fork;
    TempRepo repo;
    repo.git({"remote", "add", "upstream", upstream.dir()});
    repo.git({"remote", "add", "fork", fork.dir()});
    repo.git({"fetch", "-q", "upstream"});
    repo.git({"reset", "-q", "--hard", "upstream/main"});
    repo.git({"branch", "-q", "--set-upstream-to=upstream/main", "main"});
    repo.git({"config", "branch.main.pushRemote", "fork"});
    repo.write("b.txt", "mine\n");
    repo.commitAll("my work");

    RepoSession session;
    ASSERT_TRUE(openAndWait(session, repo));
    const auto preview = previewOf(session, gity::session::Preview::Kind::Push);
    ASSERT_TRUE(preview);
    EXPECT_TRUE(preview->newBranch);
    EXPECT_EQ(preview->remote, QStringLiteral("fork"));
    EXPECT_EQ(preview->outgoing, 1);
}

TEST(SessionCommands, ASupersededWalkSendsNothingIntoTheNewOne) {
    // The window empties its model and then asks for a filtered walk. Rows
    // the previous walk had already sent were still queued, and landed in
    // the emptied model ahead of the new walk's.
    TempRepo repo;
    for (int i = 0; i < 5; ++i) {
        repo.write("a.txt", std::to_string(i) + "\n");
        repo.commitAll(QStringLiteral("commit %1").arg(i));
    }

    RepoSession session;
    std::size_t rows = 0;
    int finishes = 0;
    QObject::connect(&session, &RepoSession::chunkReady, &session,
                     [&](gity::session::ChunkPtr chunk) { rows += chunk->rowCount(); });
    QEventLoop loop;
    QObject::connect(&session, &RepoSession::finished, &loop, [&](quint64, double, double, bool) {
        ++finishes;
        loop.quit();
    });

    session.open(repo.dir());
    // Long enough for the whole first walk to be sent, and to sit queued
    // here, unread, as it would behind a busy window.
    QThread::msleep(1000);
    session.requestFilter(gity::git::CommitFilter{});
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    loop.exec();
    // Anything still to come from either walk.
    QTimer::singleShot(300, &loop, &QEventLoop::quit);
    loop.exec();

    EXPECT_EQ(rows, 5u) << "rows from the first walk reached the second";
    EXPECT_EQ(finishes, 1);
}
