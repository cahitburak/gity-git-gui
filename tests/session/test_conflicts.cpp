// The conflict editor's reads and writes through a session, against a real
// merge that stopped on a conflict.
#include "TempRepo.h"

#include "session/RepoSession.h"

#include <gtest/gtest.h>

#include <QEventLoop>
#include <QTimer>

#include <functional>

using gity::session::ConflictPtr;
using gity::session::GitProcess;
using gity::session::RepoSession;
using gity::test::TempRepo;

namespace {

/// main and side both change the middle line of f.txt; side is merged into
/// main, which stops on the conflict.
void conflictedMerge(TempRepo& repo) {
    repo.write("f.txt", "a\nb\nc\n");
    repo.commitAll(QStringLiteral("base"));
    repo.git({"checkout", "-q", "-b", "side"});
    repo.write("f.txt", "a\nTHEIRS\nc\n");
    repo.commitAll(QStringLiteral("side"));
    repo.git({"checkout", "-q", "main"});
    repo.write("f.txt", "a\nOURS\nc\n");
    repo.commitAll(QStringLiteral("main"));
    // Fails, as it should: run without the helper's success check.
    static_cast<void>(GitProcess::run(repo.dir(), {"merge", "-q", "side"}));
}

bool unmerged(const TempRepo& repo, const QString& path) {
    return !repo.git({"ls-files", "-u", "--", path}).output.trimmed().isEmpty();
}

struct Written {
    bool ok = false;
    QString message;
    bool arrived = false;
};

/// Opens `repo`, runs `request` on the session, and waits for the write.
Written write(const TempRepo& repo, const std::function<void(RepoSession&)>& request) {
    RepoSession session;
    Written written;
    QEventLoop loop;
    QObject::connect(&session, &RepoSession::conflictWriteFinished, &loop,
                     [&](const QString&, bool ok, const QString& message) {
                         written = {ok, message, true};
                         loop.quit();
                     });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    session.open(repo.dir());
    request(session);
    loop.exec();
    return written;
}

} // namespace

TEST(Conflicts, ReadsTheVersionsAndNamesBothSides) {
    TempRepo repo;
    conflictedMerge(repo);

    RepoSession session;
    ConflictPtr conflict;
    QString problem;
    QEventLoop loop;
    QObject::connect(&session, &RepoSession::conflictReady, &loop,
                     [&](const QString&, ConflictPtr found, const QString& why) {
                         conflict = std::move(found);
                         problem = why;
                         loop.quit();
                     });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    session.open(repo.dir());
    session.requestConflict(QStringLiteral("f.txt"));
    loop.exec();

    ASSERT_TRUE(conflict) << problem.toStdString();
    EXPECT_EQ(conflict->ours.text, "a\nOURS\nc\n");
    EXPECT_EQ(conflict->theirs.text, "a\nTHEIRS\nc\n");
    EXPECT_EQ(conflict->base.text, "a\nb\nc\n");
    EXPECT_EQ(conflict->merge.conflictCount(), 1U);
    EXPECT_EQ(conflict->head.branch, "main");
    EXPECT_EQ(conflict->head.summary, "main");
    EXPECT_EQ(conflict->incoming.summary, "side");
}

TEST(Conflicts, SavingWritesTheFileAndMarkingStagesIt) {
    TempRepo repo;
    conflictedMerge(repo);

    const Written saved = write(repo, [](RepoSession& session) {
        session.requestSaveResolution(QStringLiteral("f.txt"), "a\nmine\nc\n", false);
    });
    ASSERT_TRUE(saved.arrived);
    EXPECT_TRUE(saved.ok) << saved.message.toStdString();
    EXPECT_EQ(repo.read("f.txt"), "a\nmine\nc\n");
    EXPECT_TRUE(unmerged(repo, "f.txt")) << "saving alone leaves it for the user to mark";

    const Written marked = write(repo, [](RepoSession& session) {
        session.requestSaveResolution(QStringLiteral("f.txt"), "a\nboth\nc\n", true);
    });
    EXPECT_TRUE(marked.ok) << marked.message.toStdString();
    EXPECT_FALSE(unmerged(repo, "f.txt"));
    EXPECT_EQ(repo.indexContents("f.txt"), "a\nboth\nc\n");
}

TEST(Conflicts, TakingASideWritesAndStagesIt) {
    TempRepo repo;
    conflictedMerge(repo);
    const Written taken = write(repo, [](RepoSession& session) {
        session.requestResolveWithSide(QStringLiteral("f.txt"), RepoSession::ConflictSide::Theirs);
    });
    EXPECT_TRUE(taken.ok) << taken.message.toStdString();
    EXPECT_EQ(repo.read("f.txt"), "a\nTHEIRS\nc\n");
    EXPECT_FALSE(unmerged(repo, "f.txt"));
}

TEST(Conflicts, StartingOverBringsTheConflictBack) {
    TempRepo repo;
    conflictedMerge(repo);
    static_cast<void>(write(repo, [](RepoSession& session) {
        session.requestResolveWithSide(QStringLiteral("f.txt"), RepoSession::ConflictSide::Ours);
    }));
    ASSERT_FALSE(unmerged(repo, "f.txt"));

    const Written restarted = write(repo, [](RepoSession& session) {
        session.requestRestartConflict(QStringLiteral("f.txt"));
    });
    EXPECT_TRUE(restarted.ok) << restarted.message.toStdString();
    EXPECT_TRUE(unmerged(repo, "f.txt"));
    EXPECT_NE(repo.read("f.txt").find("<<<<<<<"), std::string::npos);
}

TEST(Conflicts, AFileOneSideDeletedCanBeDeleted) {
    TempRepo repo;
    repo.write("f.txt", "a\n");
    repo.commitAll(QStringLiteral("base"));
    repo.git({"checkout", "-q", "-b", "side"});
    repo.remove("f.txt");
    repo.commitAll(QStringLiteral("side deletes"));
    repo.git({"checkout", "-q", "main"});
    repo.write("f.txt", "a\nmore\n");
    repo.commitAll(QStringLiteral("main edits"));
    static_cast<void>(GitProcess::run(repo.dir(), {"merge", "-q", "side"}));
    ASSERT_TRUE(unmerged(repo, "f.txt"));

    const Written deleted = write(repo, [](RepoSession& session) {
        session.requestResolveWithSide(QStringLiteral("f.txt"), RepoSession::ConflictSide::Delete);
    });
    EXPECT_TRUE(deleted.ok) << deleted.message.toStdString();
    EXPECT_FALSE(unmerged(repo, "f.txt"));
    EXPECT_FALSE(repo.inIndex("f.txt"));
    EXPECT_FALSE(QFileInfo::exists(QDir(repo.dir()).filePath("f.txt")));
}

TEST(Conflicts, APathOutsideTheRepositoryIsRefused) {
    TempRepo repo;
    conflictedMerge(repo);
    const Written refused = write(repo, [](RepoSession& session) {
        session.requestSaveResolution(QStringLiteral("../escape.txt"), "x", false);
    });
    EXPECT_TRUE(refused.arrived);
    EXPECT_FALSE(refused.ok);
}

TEST(Conflicts, APathIntoGitOrThroughALinkOutIsRefused) {
    // The check was on the spelling alone: ".git/hooks/…" is inside the
    // working copy by spelling, and so is "link/…" when link leads out.
    TempRepo repo;
    conflictedMerge(repo);
    QTemporaryDir outside;
    ASSERT_TRUE(outside.isValid());
    ASSERT_TRUE(QFile::link(outside.path(), QDir(repo.dir()).filePath(QStringLiteral("link"))));

    for (const QString& path :
         {QStringLiteral(".git/hooks/post-commit"), QStringLiteral(".GIT/hooks/post-commit"),
          QStringLiteral("link/escape.txt"), QStringLiteral("link/new/escape.txt")}) {
        const Written refused = write(repo, [&path](RepoSession& session) {
            session.requestSaveResolution(path, "#!/bin/sh\n", false);
        });
        EXPECT_TRUE(refused.arrived) << path.toStdString();
        EXPECT_FALSE(refused.ok) << path.toStdString();
    }
    EXPECT_FALSE(QFileInfo::exists(repo.dir() + QStringLiteral("/.git/hooks/post-commit")));
    EXPECT_TRUE(QDir(outside.path()).isEmpty()) << "a file was written outside the repository";
}

TEST(Conflicts, AResolutionInANewFolderIsStillWritten) {
    // The checks above look at what exists; a folder the file is the first
    // thing in is not there yet.
    TempRepo repo;
    conflictedMerge(repo);
    const Written saved = write(repo, [](RepoSession& session) {
        session.requestSaveResolution(QStringLiteral("fresh/deeper/f.txt"), "x\n", false);
    });
    EXPECT_TRUE(saved.ok) << saved.message.toStdString();
    EXPECT_EQ(repo.read("fresh/deeper/f.txt"), "x\n");
}
