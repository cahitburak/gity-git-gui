// ADR-004 — one session per repository, on its own thread.
//
// The UI thread never links against libgit2 symbols and never sees a
// git_repository. Everything crossing back is an immutable value type
// delivered by queued signal: here, a shared_ptr to a const HistoryChunk that
// the worker will not touch again.
#pragma once

#include "core/git/CommitDetail.h"
#include "core/git/Compare.h"
#include "ImageDiff.h"
#include "core/git/BlobReader.h"
#include "core/git/FileDiff.h"
#include "core/git/Staging.h"
#include "core/git/WorkingCopy.h"
#include "core/git/Refs.h"
#include "core/git/HistoryStream.h"
#include "core/git/HunkStaging.h"
#include "core/git/LfsLocks.h"
#include "core/git/NetworkOutcome.h"
#include "session/GitProcess.h"
#include "core/git/OperationState.h"
#include "core/git/RebaseTodo.h"
#include "core/git/Remotes.h"
#include "core/model/HistoryChunk.h"
#include "session/UndoLog.h"

#include <QObject>
#include <QString>
#include <QThread>

#include <atomic>
#include <memory>

namespace gity::session {

using ChunkPtr = std::shared_ptr<const model::HistoryChunk>;
using RefSetPtr = std::shared_ptr<const git::RefSet>;
using DetailPtr = std::shared_ptr<const git::CommitDetail>;
using FileDiffPtr = std::shared_ptr<const git::FileDiff>;
using StatusPtr = std::shared_ptr<const git::WorkingCopyStatus>;
using RemoteListPtr = std::shared_ptr<const std::vector<git::RemoteEntry>>;
using HunkStagingPtr = std::shared_ptr<const std::vector<git::HunkStaging>>;
using LockListPtr = std::shared_ptr<const std::vector<git::LfsLock>>;
using ComparisonPtr = std::shared_ptr<const git::Comparison>;

/// What an operation would do, worked out before it runs.
struct Preview {
    enum class Kind { Pull, Merge, Rebase, Push };
    Kind kind = Kind::Pull;
    QString branch; ///< The local branch acted on.
    QString target; ///< Its upstream, or the branch merged or rebased onto.
    /// Non-empty when no preview could be made; says why.
    QString problem;

    /// Commits `target` has that `branch` lacks, and the reverse, with up to
    /// ten of each described.
    int incoming = 0;
    int outgoing = 0;
    QStringList incomingCommits;
    QStringList outgoingCommits;
    bool upToDate = false;
    bool fastForward = false;

    /// From a trial merge in memory. `predicted` false when this git cannot
    /// make one; `predictionNote` then says so.
    bool predicted = false;
    QStringList conflicts;
    QString predictionNote;

    /// Uncommitted files that would make git refuse.
    QStringList blockedByLocal;

    // Push only.
    bool newBranch = false;
    QString remote;
    QString remoteBranch;
    /// The remote branch's tip as last fetched: what a force push leases
    /// against, so it fails rather than overwrite work pushed since.
    QString leaseOid;
    /// A branch other people are likely to build on: main, master, develop,
    /// trunk, release/*, or the remote's default.
    bool shared = false;
};
using PreviewPtr = std::shared_ptr<const Preview>;

struct RepoInfo {
    QString workdir;
    QString headRef;
};

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
    void applyPatch(const QString& patchText);
    void discardPatch(const QString& patchText);
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
    void workingDiffReady(gity::session::FileDiffPtr diff,
                          gity::session::HunkStagingPtr staging, quint64 generation);
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
    void historyEditFinished(const QString& verb, bool ok, bool conflicted,
                             const QString& message);
    void editStopPrepared(const QString& message, const QString& shortOid);
    void editStopChanged(const QString& subject);
    void commitPeekReady(const QString& oidHex, gity::session::DetailPtr detail);
    void commitPeekDiffReady(const QString& oidHex, const QString& path,
                             gity::session::FileDiffPtr diff);
    void commitsSinceReady(const QString& baseOid, const QStringList& oids,
                           const QStringList& subjects);
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
    /// Active commit filter; inactive walks the whole history.
    gity::git::CommitFilter filter_;
};

class RepoSession : public QObject {
    Q_OBJECT

public:
    explicit RepoSession(QObject* parent = nullptr);
    ~RepoSession() override;

    RepoSession(const RepoSession&) = delete;
    RepoSession& operator=(const RepoSession&) = delete;

    /// Queues an open. A second call supersedes the first: the in-flight walk
    /// is cancelled rather than awaited (ADR-004, backpressure).
    void open(const QString& path);
    /// As open, walking with `filter` — a different repository brings its own
    /// filter rather than inheriting the one being left.
    void open(const QString& path, const gity::git::CommitFilter& filter);

    /// Asks for one commit's detail. Newer requests supersede older ones, so
    /// holding an arrow key does not queue a diff per row.
    void requestDetail(const QString& oidHex);

    /// One file's diff within a commit. Supersedes like detail requests do.
    void requestFileDiff(const QString& oidHex, const QString& path);
    /// See commitPeekReady. An empty path asks for the file list.
    void requestCommitPeek(const QString& oidHex, const QString& path = {});

    /// Two commits — usually two branch tips — side by side. Shares the
    /// detail generation, so selecting a commit supersedes a comparison still
    /// loading and the reverse.
    void requestComparison(const QString& fromHex, const QString& toHex);
    /// One file between two commits, `from` to `to`.
    void requestFileDiffBetween(const QString& fromHex, const QString& toHex,
                                const QString& path);

    /// Commits what is staged, through the git binary so hooks and signing
    /// run. `amend` replaces the last commit instead.
    void requestCommit(const QString& message, bool amend = false);

    void requestStatus();

    /// Stages the given paths as one index write.
    void requestStage(const QStringList& paths);
    void requestUnstage(const QStringList& paths);
    void requestWorkingDiff(const QString& path, bool staged);

    /// Applies a patch the UI built from the diff it is showing. Sending the
    /// patch rather than a line selection means a stale view fails cleanly
    /// instead of staging the wrong lines.
    void requestApplyPatch(const QString& patchText);

    /// Reverts a patch from the worktree. Irreversible; the caller confirms.
    void requestDiscardPatch(const QString& patchText);

    /// ADR-003 — every network verb goes through the git binary, so the
    /// user's credential helper, SSH agent, LFS filters and hooks all behave
    /// exactly as they do in their terminal.
    /// Every remote of this repository and, unless `withSubmodules` is
    /// false, of each initialised submodule, nested ones included.
    void requestFetch(bool withSubmodules = true);
    void requestPull();
    void requestPush();
    /// Pushes one branch by name, for acting on a branch that is not checked
    /// out: to its own upstream if it has one, otherwise publishing it and
    /// setting the upstream, as requestPush does.
    void requestPushRef(const QString& branch);

    /// Clones into `parentDir/folder`. Unlike every other verb this one does
    /// not need a repository to be open, and is the one first-run path that
    /// has to work before anything else does.
    void requestClone(const QString& url, const QString& parentDir, const QString& folder);

    void requestRemotes();
    /// One `git remote …` invocation. Building the argument list here rather
    /// than taking a command string keeps the single-door rule of ADR-003.
    void requestAddRemote(const QString& name, const QString& url);
    void requestRemoveRemote(const QString& name);
    void requestRenameRemote(const QString& from, const QString& to);
    void requestSetRemoteUrl(const QString& name, const QString& url);


    /// M6 — history editing. Each leaves the repository mid-operation when it
    /// conflicts, which is a normal outcome and is reported as such.
    void requestMerge(const QString& ref);
    /// Replays the current branch's commits on top of `ref`. The plain,
    /// non-interactive rebase — the counterpart to Merge.
    void requestRebaseOnto(const QString& ref);
    void requestCherryPick(const QString& oidHex);
    void requestRevert(const QString& oidHex);
    /// Finishes or abandons whatever is underway. The verb is chosen from the
    /// detected operation, never assumed.
    /// `message` is the commit box's text, used at an edit stop.
    void requestContinueOperation(const QString& message = {});
    void requestAbortOperation();
    void requestOperationState();

    /// Re-walks history keeping only commits that match. Empty restores the
    /// full graph. A filtered view is a list of matches, not a graph — see
    /// StreamOptions::filter.
    void requestFilter(const gity::git::CommitFilter& filter);

    /// Switches the working copy to `ref`. Goes through the git binary so
    /// hooks, LFS smudge filters and sparse-checkout all behave as they would
    /// in a terminal (ADR-003).
    void requestCheckout(const QString& ref);

    /// What a checkout does with uncommitted changes.
    enum class LocalChanges { Keep, StashAndReapply, Discard };
    /// What `ref` is: a local branch, a remote branch (a local one tracking it
    /// is created, or switched to if it exists), or anything else — a tag or a
    /// commit — which is checked out detached.
    enum class RefKind { Local, Remote, Detached };

    /// The full checkout: local changes kept, stashed and reapplied, or
    /// discarded; then, if asked, submodules moved to the commits the new
    /// branch records — git's switch leaves them where they were, which is
    /// what makes a submodule look "changed" after every branch switch. One
    /// undoable step.
    void requestCheckout(const QString& ref, RefKind kind, LocalChanges changes,
                         bool updateSubmodules);
    /// Creates `name` at `startPoint` (empty means HEAD), optionally switching
    /// to it with the same choices a checkout offers. `kind` says what the
    /// start point is: a new name from a remote branch does not track it.
    void requestCreateBranch(const QString& name, const QString& startPoint, RefKind kind,
                             bool checkout, LocalChanges changes = LocalChanges::Keep,
                             bool updateSubmodules = false);
    /// `force` uses -D, which discards commits the branch alone holds.
    void requestDeleteBranch(const QString& name, bool force);

    /// Tags. `message` empty creates a lightweight tag — a bare pointer at a
    /// commit; non-empty creates an annotated one, which carries a tagger,
    /// a date and a message, and is what a release should be.
    void requestCreateTag(const QString& name, const QString& target, const QString& message);
    void requestDeleteTag(const QString& name);
    /// Tags are not pushed by `git push`; they need naming explicitly, which
    /// is the usual reason a tag exists locally and nowhere else.
    void requestPushTag(const QString& name);

    /// Moves the current branch to `target`.
    ///
    /// `mode` is git's: soft keeps the index and worktree, mixed keeps the
    /// worktree and clears the index, hard discards both.
    void requestReset(const QString& target, const QString& mode);

    /// Stashes the working copy. `includeUntracked` decides whether new files
    /// go with it; without it they stay behind and the stash is not the whole
    /// change.
    /// Stashes the working copy, or only `paths` when given.
    void requestStashSave(const QString& message, bool includeUntracked,
                          const QStringList& paths = {});
    /// `ref` is a stash reference such as "stash@{0}". `pop` removes it once
    /// applied.
    void requestStashApply(const QString& ref, bool pop);
    void requestStashDrop(const QString& ref);

    /// LFS locking. The list is advisory and asking for it costs a network
    /// round trip, so it is refreshed on demand rather than polled.
    void requestLocks();
    /// Returns the given paths to their state in the last commit, deleting
    /// them if they are untracked. Destroys work by design — the caller must
    /// have confirmed.
    void requestDiscardPaths(const QStringList& paths);
    void requestLock(const QString& path);
    /// `force` overrides git-lfs's refusal to unlock a file with uncommitted
    /// changes — a refusal worth respecting, since unlocking announces you are
    /// finished with a file you are visibly still editing.
    void requestUnlock(const QString& path, bool force = false);

    /// Runs `rebase -i` with `steps` as the instruction list, newest first.
    /// Validate with `validateTodo` before calling — an invalid list makes git
    /// abort partway and leaves the repository mid-rebase.
    void requestInteractiveRebase(const QString& baseOid,
                                  const std::vector<git::RebaseStep>& steps);
    /// The commits a rebase onto `baseOid` would replay, for the dialog to
    /// list. Asked of git rather than read off the graph: the graph shows
    /// every branch, and only HEAD's own ancestry is being rewritten.
    void requestCommitsSince(const QString& baseOid);


    /// Decodes both sides of a binary asset. Supersedes like the other
    /// selection-driven requests.
    ///
    /// With `commitOid`, the sides are that commit and its first parent —
    /// what the commit changed. Without, HEAD and the working copy.
    ///
    /// With `baseOid` as well, the sides are that commit and `commitOid`.
    void requestImageDiff(const QString& path, const QString& commitOid = {},
                          const QString& baseOid = {});


    /// Puts back what the last recorded operation changed, or reverses the
    /// last undo.
    void requestUndo();
    void requestRedo();

    /// Which account this repository uses for its remote; answers with
    /// accountChoiceReady.
    void requestAccountChoice();
    /// Makes this repository — and its submodules on the same host — use
    /// `account` for that host, in their own config only. Empty returns them
    /// to git's default.
    void requestSetAccount(const QString& account);

    /// Works out what a pull, merge, rebase or push would do, for the user to
    /// see before it happens. `target` is the branch merged or rebased onto;
    /// `branch` the local branch pushed (empty: the current one). A pull
    /// fetches first, so its preview is of the remote as it is now.
    void requestPreview(Preview::Kind kind, const QString& target = {},
                        const QString& branch = {});
    /// Force push with a lease: see RepoWorker::runForcePush.
    void requestForcePush(const QString& branch, const QString& remote,
                          const QString& remoteBranch, const QString& leaseOid);

    void cancel();

signals:
    void undoStateChanged(const QString& undoLabel, const QString& redoLabel);
    void accountChoiceReady(const QString& host, bool viaGitHubCli, const QStringList& accounts,
                            const QString& defaultAccount, const QString& chosen);
    void previewReady(gity::session::PreviewPtr preview);
    void opened(const gity::session::RepoInfo& info);
    void refsReady(gity::session::RefSetPtr refs);
    void chunkReady(gity::session::ChunkPtr chunk);
    void detailReady(gity::session::DetailPtr detail);
    void comparisonReady(gity::session::ComparisonPtr comparison);
    void fileDiffReady(gity::session::FileDiffPtr diff);
    void statusReady(gity::session::StatusPtr status);
    void imageDiffReady(gity::session::ImageDiffPtr diff);
    /// `staging` carries one entry per hunk; empty when the file has no
    /// staged changes to report against.
    void workingDiffReady(gity::session::FileDiffPtr diff,
                          gity::session::HunkStagingPtr staging);
    void committed(const QString& summary);
    void commitFailed(const QString& message, const QString& detail);
    void finished(quint64 commits, double firstChunkMs, double totalMs, bool cancelled);
    void failed(const QString& message, const QString& detail);
    void networkStarted(const QString& verb);
    void networkProgress(const QString& phase, int percent);
    void networkFinished(const QString& verb, bool ok, const QString& summary,
                         const QString& detail);
    /// The clone succeeded and this path is a repository worth opening.
    void cloneFinished(const QString& path);
    void remotesReady(gity::session::RemoteListPtr remotes);
    void remoteCommandFailed(const QString& message);
    /// The repository's in-progress operation, as a gity::git::Operation.
    void operationChanged(int operation, const QString& noun);
    /// `conflicted` distinguishes "stopped, needs you" from "failed"; the
    /// first is a normal outcome of these verbs, not an error.
    void historyEditFinished(const QString& verb, bool ok, bool conflicted,
                             const QString& message);
    /// Commits reachable from HEAD but not from `baseOid`, newest first.
    /// A rebase stopped to edit a commit, whose changes are now staged:
    /// `message` is its message, for the commit box.
    void editStopPrepared(const QString& message, const QString& shortOid);
    /// The commit being edited, by summary; empty when none.
    void editStopChanged(const QString& subject);
    /// A commit's files (and, with a path, one file's diff) for a view of its
    /// own — the rebase dialog — without moving the main window's selection,
    /// which listens to detailReady.
    void commitPeekReady(const QString& oidHex, gity::session::DetailPtr detail);
    void commitPeekDiffReady(const QString& oidHex, const QString& path,
                             gity::session::FileDiffPtr diff);
    void commitsSinceReady(const QString& baseOid, const QStringList& oids,
                           const QStringList& subjects);
    /// `supported` false means the remote has no locking API, which is a
    /// normal answer and not an error.
    void locksReady(gity::session::LockListPtr locks, bool supported);

private:
    QThread thread_;
    RepoWorker* worker_ = nullptr;
    std::atomic<bool> cancel_{false};
    /// Set once, when the session is destroyed. Kept apart from cancel_,
    /// which every reopen raises: a reopen must not kill a fetch in flight.
    std::atomic<bool> shutdown_{false};
    quint64 detailGeneration_ = 0;
    quint64 diffGeneration_ = 0;
    quint64 workingDiffGeneration_ = 0;
    quint64 imageGeneration_ = 0;
};

} // namespace gity::session

Q_DECLARE_METATYPE(gity::session::RepoInfo)
Q_DECLARE_METATYPE(gity::session::ChunkPtr)
Q_DECLARE_METATYPE(gity::session::RefSetPtr)
Q_DECLARE_METATYPE(gity::session::DetailPtr)
Q_DECLARE_METATYPE(gity::session::ComparisonPtr)
Q_DECLARE_METATYPE(gity::session::PreviewPtr)
Q_DECLARE_METATYPE(gity::session::FileDiffPtr)
Q_DECLARE_METATYPE(gity::session::StatusPtr)
