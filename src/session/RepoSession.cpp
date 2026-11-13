#include "RepoSession.h"
#include "RepoWorker.h"

namespace gity::session {

// Defined here rather than in the header: only this file queues work, and
// only here is RepoWorker a complete type.
template <typename Fn> void RepoSession::post(Fn&& fn, Qt::ConnectionType) {
    writesInFlight_.fetch_add(1, std::memory_order_relaxed);
    QMetaObject::invokeMethod(
        worker_,
        [this, fn = std::forward<Fn>(fn)]() mutable {
            fn();
            writesInFlight_.fetch_sub(1, std::memory_order_relaxed);
        },
        Qt::QueuedConnection);
}

template <typename Fn> void RepoSession::postRead(Fn&& fn, Qt::ConnectionType) {
    QMetaObject::invokeMethod(worker_, std::forward<Fn>(fn), Qt::QueuedConnection);
}

RepoSession::RepoSession(QObject* parent) : QObject(parent) {
    qRegisterMetaType<RepoInfo>("gity::session::RepoInfo");
    qRegisterMetaType<ChunkPtr>("gity::session::ChunkPtr");
    qRegisterMetaType<RefSetPtr>("gity::session::RefSetPtr");
    qRegisterMetaType<DetailPtr>("gity::session::DetailPtr");
    qRegisterMetaType<ComparisonPtr>("gity::session::ComparisonPtr");
    qRegisterMetaType<PreviewPtr>("gity::session::PreviewPtr");
    qRegisterMetaType<FileDiffPtr>("gity::session::FileDiffPtr");
    qRegisterMetaType<StatusPtr>("gity::session::StatusPtr");
    qRegisterMetaType<ImageDiffPtr>("gity::session::ImageDiffPtr");
    qRegisterMetaType<BlamePtr>("gity::session::BlamePtr");

    worker_ = new RepoWorker(cancel_);
    worker_->moveToThread(&thread_);

    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &RepoWorker::opened, this, &RepoSession::opened);
    connect(worker_, &RepoWorker::refsReady, this, &RepoSession::refsReady);
    connect(worker_, &RepoWorker::chunkReady, this, &RepoSession::chunkReady);
    connect(worker_, &RepoWorker::detailReady, this, [this](DetailPtr detail, quint64 generation) {
        // Drop results the user has already scrolled past.
        if (generation == detailGeneration_) {
            emit detailReady(std::move(detail));
        }
    });
    connect(worker_, &RepoWorker::comparisonReady, this,
            [this](ComparisonPtr comparison, quint64 generation) {
                if (generation == detailGeneration_) {
                    emit comparisonReady(std::move(comparison));
                }
            });
    connect(worker_, &RepoWorker::fileDiffReady, this,
            [this](FileDiffPtr diff, quint64 generation) {
                if (generation == diffGeneration_) {
                    emit fileDiffReady(std::move(diff));
                }
            });
    connect(worker_, &RepoWorker::blameReady, this,
            [this](const QString& path, const QString& revision, BlamePtr blame,
                   const QString& problem, quint64 generation) {
                if (generation == blameGeneration_) {
                    emit blameReady(path, revision, std::move(blame), problem);
                }
            });
    connect(worker_, &RepoWorker::statusReady, this, &RepoSession::statusReady);
    connect(worker_, &RepoWorker::networkStarted, this, [this](const QString& verb) {
        networkBusy_.store(true, std::memory_order_relaxed);
        emit networkStarted(verb);
    });
    connect(worker_, &RepoWorker::networkProgress, this, &RepoSession::networkProgress);
    connect(worker_, &RepoWorker::cloneFinished, this, [this](const QString& path) {
        networkBusy_.store(false, std::memory_order_relaxed);
        emit cloneFinished(path);
    });
    connect(worker_, &RepoWorker::remotesReady, this, &RepoSession::remotesReady);
    connect(worker_, &RepoWorker::remoteCommandFailed, this, &RepoSession::remoteCommandFailed);
    connect(worker_, &RepoWorker::operationChanged, this, &RepoSession::operationChanged);
    connect(worker_, &RepoWorker::historyEditFinished, this, &RepoSession::historyEditFinished);
    connect(worker_, &RepoWorker::commitsSinceReady, this, &RepoSession::commitsSinceReady);
    connect(worker_, &RepoWorker::headAncestorsReady, this, &RepoSession::headAncestorsReady);
    connect(worker_, &RepoWorker::externalChange, this, &RepoSession::externalChange);
    connect(worker_, &RepoWorker::editStopPrepared, this, &RepoSession::editStopPrepared);
    connect(worker_, &RepoWorker::editStopChanged, this, &RepoSession::editStopChanged);
    connect(worker_, &RepoWorker::commitPeekReady, this, &RepoSession::commitPeekReady);
    connect(worker_, &RepoWorker::commitPeekDiffReady, this, &RepoSession::commitPeekDiffReady);
    connect(worker_, &RepoWorker::locksReady, this, &RepoSession::locksReady);
    connect(worker_, &RepoWorker::undoStateChanged, this, &RepoSession::undoStateChanged);
    connect(worker_, &RepoWorker::previewReady, this, &RepoSession::previewReady);
    connect(worker_, &RepoWorker::accountChoiceReady, this, &RepoSession::accountChoiceReady);
    connect(worker_, &RepoWorker::networkFinished, this,
            [this](const QString& verb, bool ok, const QString& summary, const QString& detail) {
                networkBusy_.store(false, std::memory_order_relaxed);
                emit networkFinished(verb, ok, summary, detail);
            });
    connect(worker_, &RepoWorker::imageDiffReady, this,
            [this](ImageDiffPtr diff, quint64 generation) {
                if (generation == imageGeneration_) {
                    emit imageDiffReady(std::move(diff));
                }
            });
    connect(worker_, &RepoWorker::workingDiffReady, this,
            [this](FileDiffPtr diff, HunkStagingPtr staging, quint64 generation) {
                if (generation == workingDiffGeneration_) {
                    emit workingDiffReady(std::move(diff), std::move(staging));
                }
            });
    connect(worker_, &RepoWorker::committed, this, &RepoSession::committed);
    connect(worker_, &RepoWorker::commitFailed, this, &RepoSession::commitFailed);
    connect(worker_, &RepoWorker::finished, this, &RepoSession::finished);
    connect(worker_, &RepoWorker::failed, this, &RepoSession::failed);

    thread_.start();
    // Registered on the worker thread itself, since the flag is per thread.
    QMetaObject::invokeMethod(
        worker_, [this] { GitProcess::setThreadAbortFlag(&stopNetwork_); }, Qt::QueuedConnection);
}

RepoSession::~RepoSession() {
    // Both flags: cancel_ stops a history walk, stopNetwork_ stops a network
    // command. Without the second, closing a tab mid-fetch froze the window
    // until the fetch finished or hit its ten-minute timeout. A local command
    // — a commit, a rebase step, a checkout — is left to finish: killing one
    // half way leaves the repository in a state nobody chose. The window asks
    // before closing while one is running.
    stopNetwork_.store(true, std::memory_order_relaxed);
    cancel_.store(true, std::memory_order_relaxed);
    thread_.quit();
    thread_.wait();
}

void RepoSession::cancelNetwork() {
    if (!networkBusy()) {
        return;
    }
    stopNetwork_.store(true, std::memory_order_relaxed);
    // Lowered again once everything already queued has run, so the next
    // fetch the user asks for is not cancelled too.
    QMetaObject::invokeMethod(
        worker_, [this] { stopNetwork_.store(false, std::memory_order_relaxed); },
        Qt::QueuedConnection);
}

void RepoSession::open(const QString& path) {
    // Supersede rather than queue behind: cancel the in-flight walk, then let
    // the new request run once the worker returns to the event loop. A walk
    // queued behind a newer one is skipped (see startWalk).
    const quint64 generation = ++walkGeneration_;
    cancel_.store(true, std::memory_order_relaxed);
    postRead([this, path, generation] {
        if (startWalk(generation)) {
            worker_->open(path);
        }
    });
}

bool RepoSession::startWalk(quint64 generation) {
    // Lowered before the check, not after: a newer request raising cancel_
    // between the two would otherwise be undone here, and the older walk
    // would run to the end.
    cancel_.store(false, std::memory_order_relaxed);
    return generation == walkGeneration_.load(std::memory_order_relaxed);
}

void RepoSession::open(const QString& path, const gity::git::CommitFilter& filter) {
    const quint64 generation = ++walkGeneration_;
    cancel_.store(true, std::memory_order_relaxed);
    postRead([this, path, filter, generation] {
        if (startWalk(generation)) {
            worker_->setFilter(filter);
            worker_->open(path);
        }
    });
}

void RepoSession::requestDetail(const QString& oidHex) {
    const quint64 generation = ++detailGeneration_;
    postRead(
        [this, oidHex, generation] {
            if (generation == detailGeneration_) { // already superseded: skip it
                worker_->loadDetail(oidHex, generation);
            }
        },
        Qt::QueuedConnection);
}

void RepoSession::requestCommitPeek(const QString& oidHex, const QString& path) {
    postRead([this, oidHex, path] { worker_->loadCommitPeek(oidHex, path); }, Qt::QueuedConnection);
}

void RepoSession::requestFileDiff(const QString& oidHex, const QString& path) {
    const quint64 generation = ++diffGeneration_;
    postRead(
        [this, oidHex, path, generation] {
            if (generation == diffGeneration_) {
                worker_->loadFileDiff(oidHex, path, generation);
            }
        },
        Qt::QueuedConnection);
}

void RepoSession::requestBlame(const QString& path, const QString& revision) {
    const quint64 generation = ++blameGeneration_;
    postRead([this, path, revision, generation] {
        if (generation == blameGeneration_) {
            worker_->loadBlame(path, revision, generation);
        }
    });
}

void RepoSession::requestComparison(const QString& fromHex, const QString& toHex) {
    const quint64 generation = ++detailGeneration_;
    postRead(
        [this, fromHex, toHex, generation] {
            if (generation == detailGeneration_) {
                worker_->loadComparison(fromHex, toHex, generation);
            }
        },
        Qt::QueuedConnection);
}

void RepoSession::requestFileDiffBetween(const QString& fromHex, const QString& toHex,
                                         const QString& path) {
    const quint64 generation = ++diffGeneration_;
    postRead(
        [this, fromHex, toHex, path, generation] {
            if (generation == diffGeneration_) {
                worker_->loadFileDiffBetween(fromHex, toHex, path, generation);
            }
        },
        Qt::QueuedConnection);
}

void RepoSession::requestStatus() {
    postRead([this] { worker_->refreshStatus(); }, Qt::QueuedConnection);
}

void RepoSession::requestStage(const QStringList& paths) {
    post([this, paths] { worker_->stage(paths); }, Qt::QueuedConnection);
}

void RepoSession::requestUnstage(const QStringList& paths) {
    post([this, paths] { worker_->unstage(paths); }, Qt::QueuedConnection);
}

void RepoSession::requestWorkingDiff(const QString& path, bool staged) {
    const quint64 generation = ++workingDiffGeneration_;
    postRead(
        [this, path, staged, generation] {
            if (generation == workingDiffGeneration_) {
                worker_->loadWorkingFileDiff(path, staged, generation);
            }
        },
        Qt::QueuedConnection);
}

void RepoSession::requestImageDiff(const QString& path, const QString& commitOid,
                                   const QString& baseOid) {
    const quint64 generation = ++imageGeneration_;
    postRead(
        [this, path, commitOid, baseOid, generation] {
            if (generation == imageGeneration_) {
                worker_->loadImageDiff(path, commitOid, baseOid, generation);
            }
        },
        Qt::QueuedConnection);
}

void RepoSession::requestFetch(bool withSubmodules) {
    post([this, withSubmodules] { worker_->runFetch(withSubmodules); }, Qt::QueuedConnection);
}

void RepoSession::requestPull() {
    post(
        [this] {
            // --ff-only rather than a merge: a client that silently creates
            // merge commits on pull is how a history fills with merges nobody
            // intended.
            worker_->runNetwork(QStringLiteral("Pull"),
                                {QStringLiteral("pull"), QStringLiteral("--ff-only"),
                                 QStringLiteral("--progress")});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestPush() {
    post([this] { worker_->runPush(); }, Qt::QueuedConnection);
}

void RepoSession::requestPushRef(const QString& branch) {
    post([this, branch] { worker_->runPushBranch(branch); }, Qt::QueuedConnection);
}

void RepoSession::requestClone(const QString& url, const QString& parentDir,
                               const QString& folder) {
    post([this, url, parentDir, folder] { worker_->runClone(url, parentDir, folder); },
         Qt::QueuedConnection);
}

void RepoSession::requestRemotes() {
    postRead([this] { worker_->loadRemotes(); }, Qt::QueuedConnection);
}

void RepoSession::requestAddRemote(const QString& name, const QString& url) {
    post(
        [this, name, url] {
            worker_->runRemoteCommand(
                {QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("--"), name, url});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestRemoveRemote(const QString& name) {
    post(
        [this, name] {
            worker_->runRemoteCommand({QStringLiteral("remote"), QStringLiteral("remove"), name});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestRenameRemote(const QString& from, const QString& to) {
    post(
        [this, from, to] {
            worker_->runRemoteCommand(
                {QStringLiteral("remote"), QStringLiteral("rename"), from, to});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestSetRemoteUrl(const QString& name, const QString& url) {
    post(
        [this, name, url] {
            worker_->runRemoteCommand({QStringLiteral("remote"), QStringLiteral("set-url"),
                                       QStringLiteral("--"), name, url});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestMerge(const QString& ref) {
    post(
        [this, ref] {
            // --no-ff is not forced: a fast-forward is what the user expects
            // when nothing has diverged. --no-edit keeps git from opening an
            // editor no one would see.
            worker_->runHistoryEdit(QStringLiteral("Merge"),
                                    {QStringLiteral("merge"), QStringLiteral("--no-edit"), ref});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestRebaseOnto(const QString& ref) {
    post(
        [this, ref] {
            worker_->runHistoryEdit(QStringLiteral("Rebase"), {QStringLiteral("rebase"), ref});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestCherryPick(const QString& oidHex) {
    post(
        [this, oidHex] {
            worker_->runHistoryEdit(
                QStringLiteral("Cherry-pick"),
                {QStringLiteral("cherry-pick"), QStringLiteral("--no-edit"), oidHex});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestRevert(const QString& oidHex) {
    post(
        [this, oidHex] {
            worker_->runHistoryEdit(
                QStringLiteral("Revert"),
                {QStringLiteral("revert"), QStringLiteral("--no-edit"), oidHex});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestContinueOperation(const QString& message) {
    post([this, message] { worker_->continueOperation(message); }, Qt::QueuedConnection);
}

void RepoSession::requestAbortOperation() {
    post(
        [this] {
            // The verb comes from what is actually on disk. Assuming "merge"
            // here is how a stopped rebase gets abandoned with the wrong
            // command.
            const char* path = git_repository_path(worker_->repository());
            const gity::git::Operation operation =
                path != nullptr ? gity::git::detectOperation(path) : gity::git::Operation::None;
            const std::string_view command = gity::git::operationCommand(operation);
            if (command.empty()) {
                return;
            }
            worker_->clearEditStop();
            worker_->runHistoryEdit(
                QStringLiteral("Abort"),
                {QString::fromUtf8(command.data(), static_cast<qsizetype>(command.size())),
                 QStringLiteral("--abort")});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestFilter(const gity::git::CommitFilter& filter) {
    // Cancels whatever walk is in flight: a filter typed while the previous
    // one is still streaming must not interleave two sets of rows.
    const quint64 generation = ++walkGeneration_;
    cancel_.store(true, std::memory_order_relaxed);
    postRead([this, filter, generation] {
        if (startWalk(generation)) {
            worker_->rewalk(filter);
        }
    });
}

void RepoSession::requestOperationState() {
    postRead([this] { worker_->reportOperation(); }, Qt::QueuedConnection);
}

void RepoSession::requestInteractiveRebase(const QString& baseOid,
                                           const std::vector<git::RebaseStep>& steps) {
    post([this, baseOid, steps] { worker_->runInteractiveRebase(baseOid, steps); },
         Qt::QueuedConnection);
}

void RepoSession::requestHeadAncestors(int limit) {
    postRead([this, limit] { worker_->loadHeadAncestors(limit); });
}

void RepoSession::requestCommitsSince(const QString& baseOid) {
    postRead([this, baseOid] { worker_->loadCommitsSince(baseOid); }, Qt::QueuedConnection);
}

void RepoSession::requestCheckout(const QString& ref) {
    post(
        [this, ref] {
            // `switch` rather than `checkout`: it refuses to silently create a
            // detached HEAD or clobber local changes, which `checkout` will do
            // in some of its many modes.
            worker_->runBranchCommand(QObject::tr("Checkout"), {QStringLiteral("switch"), ref});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestCheckout(const QString& ref, RefKind kind, LocalChanges changes,
                                  bool updateSubmodules) {
    post(
        [this, ref, kind, changes, updateSubmodules] {
            worker_->runCheckout(ref, static_cast<int>(kind), static_cast<int>(changes),
                                 updateSubmodules);
        },
        Qt::QueuedConnection);
}

void RepoSession::requestCreateBranch(const QString& name, const QString& startPoint, RefKind kind,
                                      bool checkout, LocalChanges changes, bool updateSubmodules) {
    post(
        [this, name, startPoint, kind, checkout, changes, updateSubmodules] {
            worker_->runCreateBranch(name, startPoint, static_cast<int>(kind), checkout,
                                     static_cast<int>(changes), updateSubmodules);
        },
        Qt::QueuedConnection);
}

void RepoSession::requestDeleteBranch(const QString& name, bool force) {
    post(
        [this, name, force] {
            worker_->runBranchCommand(QObject::tr("Delete branch"),
                                      {QStringLiteral("branch"),
                                       force ? QStringLiteral("-D") : QStringLiteral("-d"), name});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestCreateTag(const QString& name, const QString& target,
                                   const QString& message) {
    post(
        [this, name, target, message] {
            QStringList args{QStringLiteral("tag")};
            if (!message.isEmpty()) {
                // Annotated: an object of its own with an author and a date.
                args << QStringLiteral("--annotate") << QStringLiteral("--message") << message;
            }
            args << name;
            if (!target.isEmpty()) {
                args << target;
            }
            worker_->runBranchCommand(QObject::tr("Create tag"), args);
        },
        Qt::QueuedConnection);
}

void RepoSession::requestDeleteTag(const QString& name) {
    post(
        [this, name] {
            worker_->runBranchCommand(QObject::tr("Delete tag"),
                                      {QStringLiteral("tag"), QStringLiteral("--delete"), name});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestPushTag(const QString& name) {
    post([this, name] { worker_->runPushTag(name); }, Qt::QueuedConnection);
}

void RepoSession::requestReset(const QString& target, const QString& mode) {
    post(
        [this, target, mode] {
            worker_->runBranchCommand(
                QObject::tr("Reset"),
                {QStringLiteral("reset"), QStringLiteral("--%1").arg(mode), target});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestStashSave(const QString& message, bool includeUntracked,
                                   const QStringList& paths) {
    post(
        [this, message, includeUntracked, paths] {
            // --literal-pathspecs: file names, not patterns — "[id].tsx" is a
            // file, not a character class.
            QStringList args{QStringLiteral("--literal-pathspecs"), QStringLiteral("stash"),
                             QStringLiteral("push")};
            if (includeUntracked) {
                args << QStringLiteral("--include-untracked");
            }
            if (!message.isEmpty()) {
                args << QStringLiteral("--message") << message;
            }
            if (!paths.isEmpty()) {
                args << QStringLiteral("--") << paths;
            }
            worker_->runBranchCommand(QObject::tr("Stash"), args);
        },
        Qt::QueuedConnection);
}

void RepoSession::requestStashApply(const QString& ref, bool pop) {
    post(
        [this, ref, pop] {
            worker_->runBranchCommand(pop ? QObject::tr("Pop stash") : QObject::tr("Apply stash"),
                                      {QStringLiteral("stash"),
                                       pop ? QStringLiteral("pop") : QStringLiteral("apply"), ref});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestStashDrop(const QString& ref) {
    post(
        [this, ref] {
            worker_->runBranchCommand(QObject::tr("Drop stash"),
                                      {QStringLiteral("stash"), QStringLiteral("drop"), ref});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestDiscardPaths(const QStringList& paths) {
    post([this, paths] { worker_->discardPaths(paths); }, Qt::QueuedConnection);
}

void RepoSession::requestLocks() {
    postRead([this] { worker_->loadLocks(); }, Qt::QueuedConnection);
}

void RepoSession::requestLock(const QString& path) {
    post(
        [this, path] {
            worker_->runLockCommand(
                QObject::tr("Lock"),
                {QStringLiteral("lfs"), QStringLiteral("lock"), QStringLiteral("--"), path});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestUnlock(const QString& path, bool force) {
    post(
        [this, path, force] {
            QStringList args{QStringLiteral("lfs"), QStringLiteral("unlock")};
            if (force) {
                args << QStringLiteral("--force");
            }
            args << QStringLiteral("--") << path;
            worker_->runLockCommand(QObject::tr("Unlock"), args);
        },
        Qt::QueuedConnection);
}

void RepoSession::requestDiscardPatch(const QByteArray& patch) {
    post([this, patch] { worker_->discardPatch(patch); }, Qt::QueuedConnection);
}

void RepoSession::requestApplyPatch(const QByteArray& patch) {
    post([this, patch] { worker_->applyPatch(patch); }, Qt::QueuedConnection);
}

void RepoSession::requestCommit(const QString& message, bool amend) {
    post([this, message, amend] { worker_->commit(message, amend); }, Qt::QueuedConnection);
}

void RepoSession::requestUndo() {
    post([this] { worker_->undo(); }, Qt::QueuedConnection);
}

void RepoSession::requestRedo() {
    post([this] { worker_->redo(); }, Qt::QueuedConnection);
}

void RepoSession::requestPreview(Preview::Kind kind, const QString& target, const QString& branch) {
    postRead([this, kind, target,
              branch] { worker_->computePreview(static_cast<int>(kind), target, branch); },
             Qt::QueuedConnection);
}

void RepoSession::requestForcePush(const QString& branch, const QString& remote,
                                   const QString& remoteBranch, const QString& leaseOid) {
    post([this, branch, remote, remoteBranch,
          leaseOid] { worker_->runForcePush(branch, remote, remoteBranch, leaseOid); },
         Qt::QueuedConnection);
}

void RepoSession::requestAccountChoice() {
    postRead([this] { worker_->loadAccountChoice(); }, Qt::QueuedConnection);
}

void RepoSession::requestSetAccount(const QString& account) {
    post([this, account] { worker_->setAccountChoice(account); }, Qt::QueuedConnection);
}

void RepoSession::cancel() {
    cancel_.store(true, std::memory_order_relaxed);
}

} // namespace gity::session
