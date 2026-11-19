// Commands through a whole session — the request on the UI side, the worker
// running git — against real repositories, for what only shows end to end:
// how a name typed into a dialog reaches git's argument list.
#include "TempRepo.h"

#include "session/RepoSession.h"

#include <gtest/gtest.h>

#include <QEventLoop>
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
