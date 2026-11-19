// The undo log against real repositories. ADR-011 calls the rule it keeps
// "the one everything rests on", and until these existed only its pure
// planning functions were tested.
#include "TempRepo.h"

#include "session/GitProcess.h"
#include "session/UndoLog.h"

#include <gtest/gtest.h>

#include <atomic>

using gity::session::GitProcess;
using gity::session::RepoState;
using gity::session::UndoLog;
using gity::test::TempRepo;

namespace {

QString gitDir(const TempRepo& repo) {
    return repo.dir() + QStringLiteral("/.git");
}

/// What RepoWorker::beginUndo does.
RepoState begin(const UndoLog& log, bool duringOperation = false) {
    RepoState state = log.capture(true, false);
    state.duringOperation = duringOperation;
    return state;
}

bool mergeInProgress(const TempRepo& repo) {
    return QFile::exists(gitDir(repo) + QStringLiteral("/MERGE_HEAD"));
}

} // namespace

TEST(UndoLog, UndoingACommitPutsTheBranchAndFilesBack) {
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();
    const QString base = repo.head();

    UndoLog log;
    log.load(repo.dir(), gitDir(repo));
    RepoState before = begin(log);
    repo.write("a.txt", "two\n");
    repo.commitAll("second");
    log.record(QStringLiteral("Commit"), std::move(before), false, false);
    EXPECT_EQ(log.undoLabel(), QStringLiteral("Commit"));

    const auto outcome = log.undo(false, true);
    ASSERT_TRUE(outcome.ok) << outcome.message.toStdString();
    EXPECT_EQ(repo.head(), base);
    EXPECT_EQ(repo.read("a.txt"), "one\n");
    EXPECT_EQ(log.redoLabel(), QStringLiteral("Commit"));
}

TEST(UndoLog, AMergeFinishedInATerminalDoesNotSwallowTheNextCommit) {
    // The merge stops on a conflict and is recorded as waiting. It is then
    // finished outside Gity, and the next commit in Gity must be an entry of
    // its own — not folded into "Merge", where undoing the merge would take
    // the commit with it.
    TempRepo repo;
    repo.write("f.txt", "base\n");
    repo.commitAll();
    repo.git({"switch", "-q", "-c", "side"});
    repo.write("f.txt", "side\n");
    repo.commitAll("side");
    repo.git({"switch", "-q", "main"});
    repo.write("f.txt", "main\n");
    repo.commitAll("main");

    UndoLog log;
    log.load(repo.dir(), gitDir(repo));
    RepoState beforeMerge = begin(log);
    static_cast<void>(GitProcess::run(repo.dir(), {"merge", "side"}));
    ASSERT_TRUE(mergeInProgress(repo));
    log.record(QStringLiteral("Merge"), std::move(beforeMerge), true, true);
    ASSERT_TRUE(log.hasPending());

    // Finished in a terminal.
    repo.write("f.txt", "resolved\n");
    repo.git({"add", "f.txt"});
    repo.git({"commit", "-q", "--no-edit"});
    const QString mergeCommit = repo.head();

    // Then a commit in Gity, begun with nothing in progress.
    RepoState beforeCommit = begin(log, false);
    repo.write("g.txt", "new\n");
    repo.commitAll("unrelated");
    log.record(QStringLiteral("Commit"), std::move(beforeCommit), false, false);

    EXPECT_FALSE(log.hasPending());
    EXPECT_EQ(log.undoLabel(), QStringLiteral("Commit"));
    ASSERT_TRUE(log.undo(false, true).ok);
    EXPECT_EQ(repo.head(), mergeCommit) << "undoing the commit must leave the merge";
    EXPECT_EQ(log.undoLabel(), QStringLiteral("Merge"));
}

TEST(UndoLog, AStalePendingEntryIsSettledSoUndoStillWorks) {
    TempRepo repo;
    repo.write("f.txt", "base\n");
    repo.commitAll();
    repo.git({"switch", "-q", "-c", "side"});
    repo.write("f.txt", "side\n");
    repo.commitAll("side");
    repo.git({"switch", "-q", "main"});
    repo.write("f.txt", "main\n");
    repo.commitAll("main");
    const QString beforeMergeHead = repo.head();

    UndoLog log;
    log.load(repo.dir(), gitDir(repo));
    RepoState beforeMerge = begin(log);
    static_cast<void>(GitProcess::run(repo.dir(), {"merge", "side"}));
    log.record(QStringLiteral("Merge"), std::move(beforeMerge), true, true);

    repo.write("f.txt", "resolved\n");
    repo.git({"add", "f.txt"});
    repo.git({"commit", "-q", "--no-edit"});

    log.settle(false);
    EXPECT_FALSE(log.hasPending());
    ASSERT_TRUE(log.undo(false, true).ok);
    EXPECT_EQ(repo.head(), beforeMergeHead);
}

TEST(UndoLog, AnIncompleteCaptureIsNeverRecorded) {
    // A capture git did not finish has no refs to restore; an entry built
    // from one once moved HEAD and left the branch where it was.
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();

    UndoLog log;
    log.load(repo.dir(), gitDir(repo));
    RepoState before = begin(log);
    before.valid = false;
    repo.write("a.txt", "two\n");
    repo.commitAll("second");
    log.record(QStringLiteral("Commit"), std::move(before), false, false);

    EXPECT_TRUE(log.undoLabel().isEmpty());
}

TEST(UndoLog, ClosingTheSessionDoesNotStopALocalCommand) {
    // Only network commands heed the abort flag. A commit interrupted by
    // closing the window used to be killed half way.
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();
    std::atomic<bool> closing{true};
    GitProcess::setThreadAbortFlag(&closing);
    repo.write("a.txt", "two\n");
    const auto result = GitProcess::run(repo.dir(), {"commit", "-qam", "during close"});
    GitProcess::setThreadAbortFlag(nullptr);
    EXPECT_TRUE(result.ok()) << result.errorOutput.toStdString();
}

TEST(UndoLog, TwoLogsOnOneRepositoryKeepEachOthersEntries) {
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();

    UndoLog first;
    UndoLog second;
    first.load(repo.dir(), gitDir(repo));
    second.load(repo.dir(), gitDir(repo));

    RepoState before = begin(first);
    repo.write("a.txt", "two\n");
    repo.commitAll("by the first window");
    first.record(QStringLiteral("Commit"), std::move(before), false, false);

    before = begin(second);
    repo.git({"tag", "v1"});
    second.record(QStringLiteral("Tag"), std::move(before), false, false);

    UndoLog reader;
    reader.load(repo.dir(), gitDir(repo));
    ASSERT_TRUE(reader.undo(false, true).ok);
    EXPECT_EQ(reader.undoLabel(), QStringLiteral("Commit")) << "the first window's entry was lost";
}

TEST(UndoLog, ACorruptLogIsSetAsideNotSilentlyEmptied) {
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();
    QDir().mkpath(gitDir(repo) + QStringLiteral("/gity"));
    repo.write(".git/gity/undo.json", "{ not json");

    UndoLog log;
    log.load(repo.dir(), gitDir(repo));
    const QStringList aside = QDir(gitDir(repo) + QStringLiteral("/gity"))
                                  .entryList({QStringLiteral("undo.json.corrupt-*")});
    EXPECT_EQ(aside.size(), 1);
}

TEST(UndoLog, ADeletedAnnotatedTagSurvivesGarbageCollection) {
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();
    repo.git({"tag", "-a", "v1", "-m", "release"});
    const QString tagObject = repo.git({"rev-parse", "refs/tags/v1"}).output.trimmed();

    UndoLog log;
    log.load(repo.dir(), gitDir(repo));
    RepoState before = begin(log);
    repo.git({"tag", "-d", "v1"});
    log.record(QStringLiteral("Delete tag"), std::move(before), false, false);

    repo.git({"reflog", "expire", "--expire-unreachable=now", "--all"});
    repo.git({"gc", "-q", "--prune=now"});

    const auto outcome = log.undo(false, true);
    ASSERT_TRUE(outcome.ok) << outcome.message.toStdString();
    EXPECT_EQ(repo.git({"rev-parse", "refs/tags/v1"}).output.trimmed(), tagObject);
}

TEST(UndoLog, ALockedIndexStopsTheUndoBeforeAnythingMoves) {
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();

    UndoLog log;
    log.load(repo.dir(), gitDir(repo));
    RepoState before = begin(log);
    repo.write("a.txt", "two\n");
    repo.commitAll("second");
    const QString second = repo.head();
    log.record(QStringLiteral("Commit"), std::move(before), false, false);

    repo.write(".git/index.lock", "");
    const auto outcome = log.undo(false, true);
    QFile::remove(gitDir(repo) + QStringLiteral("/index.lock"));

    EXPECT_FALSE(outcome.ok);
    EXPECT_EQ(repo.head(), second) << "the branch moved although the undo refused";
    EXPECT_EQ(log.undoLabel(), QStringLiteral("Commit"));
}

TEST(UndoLog, UndoingAfterASwitchInATerminalLeavesThatBranchsFilesAlone) {
    // The commit is undone on main, but the working copy now belongs to
    // another branch: rolling the files back to main's old tree would make
    // that branch's own commit look like uncommitted deletions.
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();
    const QString base = repo.head();
    repo.git({"branch", "other"});

    UndoLog log;
    log.load(repo.dir(), gitDir(repo));
    RepoState before = begin(log);
    repo.write("a.txt", "two\n");
    repo.commitAll("second");
    log.record(QStringLiteral("Commit"), std::move(before), false, false);

    // In a terminal: over to the other branch, and a commit there.
    repo.git({"switch", "-q", "other"});
    repo.write("c.txt", "other's work\n");
    repo.commitAll("on other");
    const QString otherTip = repo.head();

    const auto outcome = log.undo(false, true);
    ASSERT_TRUE(outcome.ok) << outcome.message.toStdString();
    EXPECT_EQ(repo.git({"rev-parse", "refs/heads/main"}).output.trimmed(), base);
    EXPECT_EQ(repo.git({"symbolic-ref", "HEAD"}).output.trimmed(),
              QStringLiteral("refs/heads/other"));
    EXPECT_EQ(repo.head(), otherTip);
    EXPECT_EQ(repo.read("c.txt"), "other's work\n");
    EXPECT_TRUE(repo.git({"status", "--porcelain"}).output.trimmed().isEmpty())
        << repo.git({"status", "--porcelain"}).output.toStdString();
}

TEST(UndoLog, UndoingADiscardAfterACommitInATerminalLeavesThatCommitsFilesAlone) {
    // A discard moves no branch, so the branch moving afterwards was not in
    // the plan: the files were rolled back to before the commit, under it.
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();

    UndoLog log;
    log.load(repo.dir(), gitDir(repo));
    repo.write("a.txt", "edited\n");
    RepoState before = begin(log);
    repo.git({"checkout", "-q", "--", "a.txt"});
    log.record(QStringLiteral("Discard"), std::move(before), true, false);

    repo.write("c.txt", "terminal\n");
    repo.commitAll("in a terminal");
    const QString tip = repo.head();

    const auto outcome = log.undo(false, true);
    ASSERT_TRUE(outcome.ok) << outcome.message.toStdString();
    EXPECT_EQ(repo.head(), tip);
    EXPECT_EQ(repo.read("c.txt"), "terminal\n");
    EXPECT_TRUE(repo.git({"status", "--porcelain"}).output.trimmed().isEmpty())
        << repo.git({"status", "--porcelain"}).output.toStdString();
}

TEST(UndoLog, AnUninitialisedSubmoduleIsNotMistakenForTheRepositoryAroundIt) {
    // An uninitialised submodule is an empty directory. git run inside it
    // finds the parent repository instead, and "putting the submodule back"
    // checked the parent out at the submodule's commit.
    TempRepo repo;
    repo.write("a.txt", "one\n");
    repo.commitAll();
    const QString base = repo.head();
    repo.write("a.txt", "two\n");
    repo.commitAll("second");
    // A gitlink naming a commit the parent happens to have, and no
    // repository on disk for it.
    repo.git({"update-index", "--add", "--cacheinfo", QStringLiteral("160000,%1,sub").arg(base)});
    ASSERT_TRUE(QDir(repo.dir()).mkpath(QStringLiteral("sub")));
    repo.git({"commit", "-q", "-m", "add a submodule"});
    const QString withSubmodule = repo.head();

    UndoLog log;
    log.load(repo.dir(), gitDir(repo));
    RepoState before = begin(log);
    repo.write("a.txt", "three\n");
    repo.commitAll("third");
    log.record(QStringLiteral("Commit"), std::move(before), false, false);

    const auto outcome = log.undo(false, true);
    ASSERT_TRUE(outcome.ok) << outcome.message.toStdString();
    EXPECT_EQ(repo.git({"symbolic-ref", "HEAD"}).output.trimmed(),
              QStringLiteral("refs/heads/main"));
    EXPECT_EQ(repo.head(), withSubmodule);
    EXPECT_EQ(repo.read("a.txt"), "two\n");
}
