// The window's sessions: one per repository, and only the one on screen
// reaching the window (ADR-004).
#include "TempRepo.h"

#include "session/SessionHub.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QEventLoop>
#include <QPointer>
#include <QTimer>

using gity::session::RepoSession;
using gity::session::SessionHub;
using gity::test::TempRepo;

namespace {

/// What a signal carried, each time it fired: its first two arguments, as text.
struct Spy {
    QList<QStringList> calls;

    [[nodiscard]] qsizetype count() const { return calls.size(); }
};

void watchFailed(RepoSession* session, Spy& spy) {
    QObject::connect(
        session, &RepoSession::failed, session,
        [&spy](const QString& message, const QString&) { spy.calls << QStringList{message}; });
}

void watchNetwork(RepoSession* session, Spy& spy) {
    QObject::connect(session, &RepoSession::networkFinished, session,
                     [&spy](const QString& verb, bool, const QString&, const QString&) {
                         spy.calls << QStringList{verb};
                     });
}

} // namespace

TEST(SessionHub, StartsOnTheRelay) {
    SessionHub hub;
    EXPECT_EQ(hub.current(), hub.relay());
    EXPECT_FALSE(hub.currentNeverOpened());
    EXPECT_TRUE(hub.currentPath().isEmpty());
    EXPECT_TRUE(hub.busyRepositories().isEmpty());
}

TEST(SessionHub, OneSessionPerRepositoryHoweverItIsSpelled) {
    SessionHub hub;
    const QString path = QDir::tempPath() + QStringLiteral("/gity-hub-a");
    EXPECT_TRUE(hub.activate(path));
    RepoSession* first = hub.current();
    EXPECT_NE(first, hub.relay());

    EXPECT_FALSE(hub.activate(path + QStringLiteral("/../gity-hub-a")));
    EXPECT_EQ(hub.current(), first);
    EXPECT_EQ(hub.currentPath(), SessionHub::keyFor(path));
}

TEST(SessionHub, OnlyTheSessionOnScreenReachesTheRelay) {
    SessionHub hub;
    hub.activate(QDir::tempPath() + QStringLiteral("/gity-hub-a"));
    RepoSession* first = hub.current();
    hub.activate(QDir::tempPath() + QStringLiteral("/gity-hub-b"));
    RepoSession* second = hub.current();
    ASSERT_NE(first, second);

    Spy relayed;
    watchFailed(hub.relay(), relayed);
    // A result still on its way from the repository just left — a preview,
    // say — must not be acted on in the one now on screen.
    emit first->failed(QStringLiteral("from a"), {});
    EXPECT_EQ(relayed.count(), 0);

    emit second->failed(QStringLiteral("from b"), {});
    ASSERT_EQ(relayed.count(), 1);
    EXPECT_EQ(relayed.calls.at(0).at(0), QStringLiteral("from b"));

    // Back again: the first is forwarded, and only once.
    hub.activate(QDir::tempPath() + QStringLiteral("/gity-hub-a"));
    emit first->failed(QStringLiteral("from a"), {});
    emit second->failed(QStringLiteral("from b"), {});
    ASSERT_EQ(relayed.count(), 2);
    EXPECT_EQ(relayed.calls.at(1).at(0), QStringLiteral("from a"));
}

TEST(SessionHub, ARepositoryOffScreenStillReportsItsNetworkOutcome) {
    SessionHub hub;
    const QString a = QDir::tempPath() + QStringLiteral("/gity-hub-a");
    hub.activate(a);
    RepoSession* first = hub.current();
    hub.activate(QDir::tempPath() + QStringLiteral("/gity-hub-b"));

    Spy background;
    QObject::connect(&hub, &SessionHub::backgroundNetworkFinished, &hub,
                     [&background](const QString& path, const QString& verb, bool, const QString&) {
                         background.calls << QStringList{path, verb};
                     });
    Spy relayed;
    watchNetwork(hub.relay(), relayed);
    emit first->networkFinished(QStringLiteral("Fetch"), true, QStringLiteral("2 new"), {});
    EXPECT_EQ(relayed.count(), 0);
    ASSERT_EQ(background.count(), 1);
    EXPECT_EQ(background.calls.at(0).at(0), SessionHub::keyFor(a));
    EXPECT_EQ(background.calls.at(0).at(1), QStringLiteral("Fetch"));

    // The one on screen reports through the relay instead.
    emit hub.current()->networkFinished(QStringLiteral("Push"), true, {}, {});
    EXPECT_EQ(background.count(), 1);
    EXPECT_EQ(relayed.count(), 1);
}

TEST(SessionHub, RetiringTheSessionOnScreenFallsBackToTheRelay) {
    SessionHub hub;
    const QString path = QDir::tempPath() + QStringLiteral("/gity-hub-a");
    hub.activate(path);
    QPointer<RepoSession> session = hub.current();

    hub.retire(path);
    EXPECT_EQ(hub.current(), hub.relay());
    EXPECT_TRUE(session.isNull()) << "an idle session is destroyed at once";

    Spy relayed;
    watchFailed(hub.relay(), relayed);
    // A new session for the same path starts from nothing.
    EXPECT_TRUE(hub.activate(path));
    EXPECT_TRUE(hub.currentNeverOpened());
    hub.retire(QDir::tempPath() + QStringLiteral("/never-opened"));
    EXPECT_EQ(relayed.count(), 0);
}

namespace {

/// A repository whose commits take a while: a pre-commit hook that sleeps,
/// so a session committing in it stays busy long enough to be closed.
void makeCommitsSlow(const TempRepo& repo) {
    repo.write(".git/hooks/pre-commit", "#!/bin/sh\nsleep 2\n");
    QFile::setPermissions(repo.dir() + QStringLiteral("/.git/hooks/pre-commit"),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

/// Opens the session on screen and waits until it reports.
bool openCurrent(SessionHub& hub, const TempRepo& repo) {
    bool opened = false;
    QEventLoop loop;
    const auto connection = QObject::connect(hub.relay(), &RepoSession::finished, &loop,
                                             [&](quint64, double, double, bool) {
                                                 opened = true;
                                                 loop.quit();
                                             });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    hub.current()->open(repo.dir());
    loop.exec();
    QObject::disconnect(connection);
    return opened;
}

} // namespace

TEST(SessionHub, ATabReopenedWhileItsSessionIsStillBusyGetsThatSessionBack) {
    // Two sessions on one repository would each think it was the only one
    // changing it.
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();
    makeCommitsSlow(repo);
    repo.write("a.txt", "two\n");
    repo.git({"add", "a.txt"});

    SessionHub hub;
    hub.activate(repo.dir());
    ASSERT_TRUE(openCurrent(hub, repo));
    QPointer<RepoSession> session = hub.current();
    session->requestCommit(QStringLiteral("slow"));
    ASSERT_TRUE(session->busy());

    hub.retire(repo.dir());
    ASSERT_FALSE(session.isNull()) << "a busy session is kept until it is idle";
    EXPECT_FALSE(hub.busyRepositories().isEmpty());

    EXPECT_TRUE(hub.activate(repo.dir()));
    EXPECT_EQ(hub.current(), session.data());
    EXPECT_TRUE(hub.currentNeverOpened()) << "the window opens it again, as any new tab";
}

TEST(SessionHub, AClosedTabReportsNothingMore) {
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();
    makeCommitsSlow(repo);
    repo.write("a.txt", "two\n");
    repo.git({"add", "a.txt"});

    SessionHub hub;
    hub.activate(repo.dir());
    ASSERT_TRUE(openCurrent(hub, repo));
    QPointer<RepoSession> session = hub.current();
    session->requestCommit(QStringLiteral("slow"));
    hub.retire(repo.dir());
    ASSERT_FALSE(session.isNull());

    Spy background;
    QObject::connect(&hub, &SessionHub::backgroundNetworkFinished, &hub,
                     [&background](const QString& path, const QString& verb, bool, const QString&) {
                         background.calls << QStringList{path, verb};
                     });
    // What a fetch stopped by closing the tab reports as it ends.
    emit session->networkFinished(QStringLiteral("Fetch"), false,
                                  QStringLiteral("git was stopped before it finished"), {});
    EXPECT_EQ(background.count(), 0);
}

TEST(SessionHub, ARealOpenArrivesThroughTheRelay) {
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();

    SessionHub hub;
    hub.activate(repo.dir());
    EXPECT_TRUE(hub.currentNeverOpened());

    QString opened;
    QEventLoop loop;
    QObject::connect(hub.relay(), &RepoSession::opened, &loop,
                     [&](const gity::session::RepoInfo& info) {
                         opened = info.workdir;
                         loop.quit();
                     });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    hub.current()->open(repo.dir());
    loop.exec();

    EXPECT_EQ(QDir(opened).canonicalPath(), QDir(repo.dir()).canonicalPath());
    hub.markOpened();
    EXPECT_FALSE(hub.currentNeverOpened());
}
