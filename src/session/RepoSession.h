// ADR-004 — one session per repository, on its own thread.
//
// The UI thread never links against libgit2 symbols and never sees a
// git_repository. Everything crossing back is an immutable value type
// (SessionTypes.h) delivered by queued signal: here, a shared_ptr to a const
// HistoryChunk that the worker will not touch again.
//
// RepoSession is the UI thread's side: it numbers requests so newer ones
// supersede older, and queues them to its RepoWorker.
#pragma once

#include "SessionTypes.h"

#include <QObject>
#include <QString>
#include <QThread>

#include <atomic>

namespace gity::session {

class RepoWorker;

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

    /// A change to the repository is queued or running. Closing now would
    /// have to wait for it — or, for a network command, stop it.
    [[nodiscard]] bool busy() const noexcept {
        return writesInFlight_.load(std::memory_order_relaxed) > 0;
    }

    /// A fetch, pull, push or clone is running.
    [[nodiscard]] bool networkBusy() const noexcept {
        return networkBusy_.load(std::memory_order_relaxed);
    }

    /// Stops the network command running now, and any queued behind it.
    /// Local commands are never interrupted.
    void cancelNetwork();
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
    void requestFileDiffBetween(const QString& fromHex, const QString& toHex, const QString& path);

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
    void requestApplyPatch(const QByteArray& patch);

    /// Reverts a patch from the worktree. Recorded for undo; the caller confirms.
    void requestDiscardPatch(const QByteArray& patch);

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

    using LocalChanges = gity::session::LocalChanges;
    using RefKind = gity::session::RefKind;

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
    /// HEAD and up to `limit` commits below it on its first-parent line —
    /// the bases an interactive rebase of this branch can start from. The
    /// graph is no help: its rows include every other branch.
    void requestHeadAncestors(int limit);

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
    void requestPreview(Preview::Kind kind, const QString& target = {}, const QString& branch = {});
    /// Force push with a lease: see RepoWorker::runForcePush.
    void requestForcePush(const QString& branch, const QString& remote, const QString& remoteBranch,
                          const QString& leaseOid);

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
    void workingDiffReady(gity::session::FileDiffPtr diff, gity::session::HunkStagingPtr staging);
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
    void historyEditFinished(const QString& verb, bool ok, bool conflicted, const QString& message);
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
    /// HEAD and its first-parent ancestors, newest first.
    void headAncestorsReady(const QStringList& oids, const QStringList& subjects);
    /// Something changed outside Gity: RepoWatcher::Change flags.
    void externalChange(int kinds);
    /// `supported` false means the remote has no locking API, which is a
    /// normal answer and not an error.
    void locksReady(gity::session::LockListPtr locks, bool supported);

private:
    /// Lowers cancel_ for a walk about to start, and says whether it is still
    /// the newest one asked for.
    [[nodiscard]] bool startWalk(quint64 generation);

    /// Queues `fn` on the worker. A write — anything that changes the
    /// repository — is counted while queued or running, so the window can
    /// tell whether closing now would interrupt one.
    template <typename Fn> void post(Fn&& fn, Qt::ConnectionType = Qt::QueuedConnection);

    /// Queues a read: nothing is lost if it never runs.
    template <typename Fn> void postRead(Fn&& fn, Qt::ConnectionType = Qt::QueuedConnection);

    QThread thread_;
    RepoWorker* worker_ = nullptr;
    std::atomic<bool> cancel_{false};
    /// Raised to stop the network command running now — by Cancel, or when
    /// the session is destroyed. Only network commands heed it: stopping a
    /// commit, a rebase or a checkout half way would leave the repository in
    /// a state nobody chose. Kept apart from cancel_, which every reopen
    /// raises: a reopen must not kill a fetch in flight.
    std::atomic<bool> stopNetwork_{false};
    std::atomic<bool> networkBusy_{false};
    std::atomic<int> writesInFlight_{0};
    /// Bumped by every open and filter. A walk queued behind a newer one is
    /// skipped outright rather than run to the end.
    std::atomic<quint64> walkGeneration_{0};
    // Atomic because the worker reads them too: a request already superseded
    // by the time the worker reaches it is skipped, not computed and dropped.
    std::atomic<quint64> detailGeneration_{0};
    std::atomic<quint64> diffGeneration_{0};
    std::atomic<quint64> workingDiffGeneration_{0};
    std::atomic<quint64> imageGeneration_{0};
};

} // namespace gity::session
