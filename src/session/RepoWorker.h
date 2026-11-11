// The worker half of a repository session (ADR-004): lives on the session's
// thread, owns the libgit2 handle, and runs every read and command. Only
// RepoSession talks to it, by queued call; what it finds goes back by signal.
//
// One class, because every command shares the handle, the undo log and the
// cancel flag; its definitions are split by area across RepoWorker.cpp and
// the Worker*.cpp files.
#pragma once

#include "SessionTypes.h"
#include "core/git/NetworkOutcome.h"
#include "core/git/Repository.h"
#include "session/GitProcess.h"
#include "session/UndoLog.h"

#include <QObject>
#include <QString>

#include <atomic>

namespace gity::session {

/// Runs on the worker thread. Owned by RepoSession; never touched directly.
class RepoWorker : public QObject {
    Q_OBJECT

public:
    explicit RepoWorker(std::atomic<bool>& cancelFlag);

public slots:
    void open(const QString& path);

    void rewalk(const gity::git::CommitFilter& filter);
    void setFilter(const gity::git::CommitFilter& filter);
    void loadDetail(const QString& oidHex, quint64 generation);
    void loadFileDiff(const QString& oidHex, const QString& path, quint64 generation);
    void loadComparison(const QString& fromHex, const QString& toHex, quint64 generation);
    void loadFileDiffBetween(const QString& fromHex, const QString& toHex, const QString& path,
                             quint64 generation);
    void commit(const QString& message, bool amend);
    void refreshStatus();
    void stage(const QStringList& paths);
    void unstage(const QStringList& paths);
    void loadWorkingFileDiff(const QString& path, bool staged, quint64 generation);
    void applyPatch(const QByteArray& patch);
    void discardPatch(const QByteArray& patch);
    void runNetwork(const QString& verb, const QStringList& args);
    /// Fetches every remote, keeping each one's default branch current — and,
    /// with `withSubmodules`, every remote of each initialised submodule.
    void runFetch(bool withSubmodules);
    void runPush();
    void runPushBranch(const QString& branch);
    void runPushTag(const QString& name);
    void runClone(const QString& url, const QString& parentDir, const QString& folder);
    void reportProgress(const QString& line);
    void loadRemotes();
    void runHistoryEdit(const QString& verb, const QStringList& args);
    void runBranchCommand(const QString& verb, const QStringList& args);
    void runInteractiveRebase(const QString& baseOid, const std::vector<git::RebaseStep>& steps);
    void loadCommitsSince(const QString& baseOid);
    void loadHeadAncestors(int limit);
    void loadLocks();
    void runLockCommand(const QString& verb, const QStringList& args);
    void discardPaths(const QStringList& paths);
    void reportOperation();

    /// The worker's repository handle, for callers that need to read state on
    /// the worker thread before choosing a command.
    [[nodiscard]] git_repository* repository() const { return repo_.get(); }

    void runRemoteCommand(const QStringList& args);
    void loadImageDiff(const QString& path, const QString& commitOid, const QString& baseOid,
                       quint64 generation);
    void undo();
    void redo();
    void computePreview(int kind, const QString& target, const QString& branch);
    void loadAccountChoice();
    void runCheckout(const QString& ref, int kind, int changes, bool updateSubmodules);
    void loadCommitPeek(const QString& oidHex, const QString& path);
    /// Continues the operation in progress; at a prepared edit stop, commits
    /// the staged edit first with `message` (empty: the original one).
    void continueOperation(const QString& message);
    void clearEditStop();
    void runCreateBranch(const QString& name, const QString& startPoint, int kind, bool checkout,
                         int changes, bool updateSubmodules);
    void setAccountChoice(const QString& account);
    /// Pushes over the remote branch, but only if it still holds `leaseOid`.
    void runForcePush(const QString& branch, const QString& remote, const QString& remoteBranch,
                      const QString& leaseOid);

public:
    /// Captures the state an operation starts from, for undo.
    [[nodiscard]] RepoState beginUndo();
    /// Records the operation that started from `before`. `filesChanged` says
    /// the working copy may have changed even if no ref did.
    void endUndo(const QString& verb, RepoState before, bool filesChanged);

signals:
    void opened(const gity::session::RepoInfo& info);
    void refsReady(gity::session::RefSetPtr refs);
    void chunkReady(gity::session::ChunkPtr chunk);
    void detailReady(gity::session::DetailPtr detail, quint64 generation);
    void comparisonReady(gity::session::ComparisonPtr comparison, quint64 generation);
    void fileDiffReady(gity::session::FileDiffPtr diff, quint64 generation);
    void statusReady(gity::session::StatusPtr status);
    void imageDiffReady(gity::session::ImageDiffPtr diff, quint64 generation);
    void workingDiffReady(gity::session::FileDiffPtr diff, gity::session::HunkStagingPtr staging,
                          quint64 generation);
    void committed(const QString& summary);
    void commitFailed(const QString& message, const QString& detail);
    void finished(quint64 commits, double firstChunkMs, double totalMs, bool cancelled);
    void failed(const QString& message, const QString& detail);
    void networkStarted(const QString& verb);
    /// `percent` is -1 for phases git reports without one.
    void networkProgress(const QString& phase, int percent);
    void networkFinished(const QString& verb, bool ok, const QString& summary,
                         const QString& detail);
    void cloneFinished(const QString& path);
    void remotesReady(gity::session::RemoteListPtr remotes);
    void remoteCommandFailed(const QString& message);
    void operationChanged(int operation, const QString& noun);
    void historyEditFinished(const QString& verb, bool ok, bool conflicted, const QString& message);
    void editStopPrepared(const QString& message, const QString& shortOid);
    void editStopChanged(const QString& subject);
    void commitPeekReady(const QString& oidHex, gity::session::DetailPtr detail);
    void commitPeekDiffReady(const QString& oidHex, const QString& path,
                             gity::session::FileDiffPtr diff);
    void commitsSinceReady(const QString& baseOid, const QStringList& oids,
                           const QStringList& subjects);
    /// HEAD and its first-parent ancestors, newest first.
    void headAncestorsReady(const QStringList& oids, const QStringList& subjects);
    /// Something changed outside Gity: RepoWatcher::Change flags.
    void externalChange(int kinds);
    void locksReady(gity::session::LockListPtr locks, bool supported);
    /// What Undo and Redo would do now; empty when there is nothing.
    void undoStateChanged(const QString& undoLabel, const QString& redoLabel);
    void previewReady(gity::session::PreviewPtr preview);
    /// The account this repository uses for its remote's host. `accounts` are
    /// the ones available there; `chosen` empty means git's default, which is
    /// `defaultAccount` when that is known (GitHub CLI's active one). `host`
    /// empty means there is nothing to choose — an ssh or local remote.
    void accountChoiceReady(const QString& host, bool viaGitHubCli, const QStringList& accounts,
                            const QString& defaultAccount, const QString& chosen);

private:
    /// Starts watching the repository just opened, or keeps the watcher it
    /// already has for it.
    void watchRepository();
    /// Reports a staging write that failed, and refreshes status either way.
    void reportIndexWrite(const GitResult& result);
    [[nodiscard]] bool operationInProgress() const;
    [[nodiscard]] bool worktreeClean() const;
    [[nodiscard]] bool localBranchExists(const QString& name) const;
    /// .git/gity — Gity's own files for this repository.
    [[nodiscard]] QString gityDir() const;
    /// At a clean `edit` stop, moves the commit's changes back into the index
    /// so they can be edited, and remembers its author. Returns its summary,
    /// or empty when this is not such a stop.
    QString prepareEditStop();
    /// --author and --date of the commit stopped for editing, or empty.
    [[nodiscard]] QStringList editStopAuthorArgs() const;
    /// What a failed network command means, in the user's terms.
    [[nodiscard]] QString describeFailure(const QString& verb, const GitResult& result,
                                          const gity::git::NetworkOutcome& outcome) const;
    /// Runs a `git switch …` with what the user chose for their changes
    /// (RepoSession::LocalChanges), then submodules and the stash: one
    /// undoable step either way.
    void runSwitch(const QString& verb, QStringList args, const QString& target, int changes,
                   bool updateSubmodules);
    void reportUndo();

    UndoLog undo_;

    /// The remote a branch or tag with nowhere configured is published to, or
    /// empty with `problem` saying why there is no sensible choice.
    [[nodiscard]] QString publishRemote(QString* problem) const;

    std::atomic<bool>& cancel_;
    /// Kept open between requests so detail loads do not reopen the repository.
    /// Thread-confined to the worker by construction (ADR-004).
    gity::git::RepositoryHandle repo_;
    QString workdir_;
    /// Changes made outside Gity; a child, so it goes with the worker.
    class RepoWatcher* watcher_ = nullptr;
    /// Active commit filter; inactive walks the whole history.
    gity::git::CommitFilter filter_;
};

} // namespace gity::session
