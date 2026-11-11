// RepoWorker: opening the repository, watching it, the read-only loads a
// selection asks for, and the undo log. The commands it runs are split by
// area across the Worker*.cpp files beside this one; all of them are one
// class on one thread, with one repository handle (ADR-004).
#include "RepoWorker.h"

#include "RepoWatcher.h"

#include <QDir>

namespace gity::session {

namespace {

QString headRefName(git_repository* repo) {
    git_reference* raw = nullptr;
    if (git_repository_head(&raw, repo) < 0) {
        return QObject::tr("no commits yet");
    }
    const gity::git::Handle<git_reference, git_reference_free> head(raw);
    const char* shorthand = git_reference_shorthand(head.get());
    const QString name =
        shorthand != nullptr ? QString::fromUtf8(shorthand) : QStringLiteral("HEAD");

    // Said, not implied. A detached HEAD's shorthand is the short id, which
    // reads exactly like a branch name — and a commit made here is lost by the
    // next checkout, which is not a surprise anyone should get afterwards.
    if (git_repository_head_detached(repo) == 1) {
        return QObject::tr("detached at %1").arg(name);
    }
    return name;
}

} // namespace

RepoWorker::RepoWorker(std::atomic<bool>& cancelFlag) : cancel_(cancelFlag) {}

void RepoWorker::setFilter(const gity::git::CommitFilter& filter) {
    filter_ = filter;
}

void RepoWorker::rewalk(const gity::git::CommitFilter& filter) {
    if (workdir_.isEmpty()) {
        return;
    }
    filter_ = filter;
    // Re-opening is the whole walk again, which is the honest cost: a filtered
    // view is a different set of commits with different lanes, not the same
    // graph with rows hidden.
    open(workdir_);
}

void RepoWorker::open(const QString& path) {
    try {
        // One git_repository per worker thread. Nothing derived from it leaves
        // this function.
        repo_ = gity::git::openRepository(path.toStdString());
        git_repository* repo = repo_.get();

        const char* workdirRaw = git_repository_workdir(repo);
        RepoInfo info;
        // git_repository_workdir returns a trailing slash and the caller's
        // path does not. Everything downstream compares these as strings — the
        // repository tabs did, and every reopen after a branch delete, merge or
        // fetch looked like a different repository and wiped the tab set. One
        // spelling leaves here.
        info.workdir =
            QDir(workdirRaw != nullptr ? QString::fromUtf8(workdirRaw) : path).absolutePath();
        workdir_ = info.workdir;
        info.headRef = headRefName(repo);
        emit opened(info);
        undo_.load(workdir_, QString::fromUtf8(git_repository_path(repo)));
        // A merge left waiting when Gity last closed may have been finished
        // in a terminal since.
        undo_.settle(operationInProgress());
        reportUndo();

        // What is read from here on covers every change seen so far.
        if (watcher_ != nullptr && watcher_->workdir() == workdir_) {
            watcher_->absorb();
        }

        // Refs before history: the sidebar is the first thing that can be
        // filled, and it is cheap next to the walk.
        emit refsReady(std::make_shared<const gity::git::RefSet>(gity::git::loadRefs(repo)));
        refreshStatus();

        gity::git::StreamOptions options;
        options.filter = filter_;
        options.cancel = &cancel_;
        const auto stats =
            gity::git::streamHistory(repo, options, [this](model::HistoryChunk&& chunk) {
                if (cancel_.load(std::memory_order_relaxed)) {
                    return false;
                }
                emit chunkReady(std::make_shared<const model::HistoryChunk>(std::move(chunk)));
                return true;
            });

        emit finished(static_cast<quint64>(stats.commits), stats.firstChunkMs, stats.totalMs,
                      stats.cancelled);
        // After the walk, not before it: setting up the watches walks the
        // directory tree, and the first rows of history matter more.
        watchRepository();
    } catch (const gity::git::GitException& e) {
        emit failed(QString::fromStdString(e.error().message),
                    QString::fromStdString(e.error().raw));
    } catch (const std::exception& e) {
        emit failed(QString::fromUtf8(e.what()), {});
    }
}

void RepoWorker::watchRepository() {
    if (!repo_) {
        return;
    }
    if (watcher_ != nullptr && watcher_->workdir() == QDir(workdir_).absolutePath()) {
        return; // a reopen of the same repository keeps its watches
    }
    delete watcher_;
    watcher_ = nullptr;
    const char* gitDir = git_repository_path(repo_.get());
    const char* commonDir = git_repository_commondir(repo_.get());
    if (gitDir == nullptr || git_repository_is_bare(repo_.get()) != 0) {
        return;
    }
    // Asked of libgit2, on this thread, which owns the repository handle.
    // A directory is asked about with a trailing slash, so patterns that only
    // match directories ("build/") apply.
    auto ignored = [this](const QString& relative, bool isDirectory) {
        int result = 0;
        const QByteArray path = (isDirectory ? relative + QChar('/') : relative).toUtf8();
        return git_ignore_path_is_ignored(&result, repo_.get(), path.constData()) == 0 &&
               result != 0;
    };
    watcher_ = new RepoWatcher(workdir_, QString::fromUtf8(gitDir),
                               QString::fromUtf8(commonDir != nullptr ? commonDir : gitDir),
                               std::move(ignored), this);
    connect(watcher_, &RepoWatcher::changed, this, &RepoWorker::externalChange);
}

void RepoWorker::loadDetail(const QString& oidHex, quint64 generation) {
    if (!repo_) {
        return;
    }
    git_oid oid{};
    if (git_oid_fromstr(&oid, oidHex.toLatin1().constData()) < 0) {
        return;
    }
    try {
        emit detailReady(std::make_shared<const gity::git::CommitDetail>(
                             gity::git::loadCommitDetail(repo_.get(), oid)),
                         generation);
    } catch (const gity::git::GitException&) {
        // A detail load failing is not worth a dialog: the row simply shows
        // nothing, and the next selection tries again.
    }
}

void RepoWorker::loadCommitPeek(const QString& oidHex, const QString& path) {
    if (!repo_) {
        return;
    }
    git_oid oid{};
    if (git_oid_fromstr(&oid, oidHex.toLatin1().constData()) < 0) {
        return;
    }
    try {
        if (path.isEmpty()) {
            emit commitPeekReady(oidHex, std::make_shared<const gity::git::CommitDetail>(
                                             gity::git::loadCommitDetail(repo_.get(), oid)));
        } else {
            emit commitPeekDiffReady(
                oidHex, path,
                std::make_shared<const gity::git::FileDiff>(
                    gity::git::loadFileDiff(repo_.get(), oid, path.toStdString())));
        }
    } catch (const gity::git::GitException&) {
        // As loadDetail: the pane shows nothing, and the next pick tries again.
    }
}

void RepoWorker::loadFileDiff(const QString& oidHex, const QString& path, quint64 generation) {
    if (!repo_) {
        return;
    }
    git_oid oid{};
    if (git_oid_fromstr(&oid, oidHex.toLatin1().constData()) < 0) {
        return;
    }
    try {
        emit fileDiffReady(std::make_shared<const gity::git::FileDiff>(
                               gity::git::loadFileDiff(repo_.get(), oid, path.toStdString())),
                           generation);
    } catch (const gity::git::GitException&) {
        // Same reasoning as loadDetail: the pane shows nothing and the next
        // selection tries again, which beats a dialog per failed diff.
    }
}

void RepoWorker::loadComparison(const QString& fromHex, const QString& toHex, quint64 generation) {
    if (!repo_) {
        return;
    }
    git_oid from{};
    git_oid to{};
    if (git_oid_fromstr(&from, fromHex.toLatin1().constData()) < 0 ||
        git_oid_fromstr(&to, toHex.toLatin1().constData()) < 0) {
        return;
    }
    try {
        emit comparisonReady(std::make_shared<const gity::git::Comparison>(
                                 gity::git::loadComparison(repo_.get(), from, to)),
                             generation);
    } catch (const gity::git::GitException& e) {
        qWarning("gity: could not compare: %s", e.error().message.c_str());
    }
}

void RepoWorker::loadFileDiffBetween(const QString& fromHex, const QString& toHex,
                                     const QString& path, quint64 generation) {
    if (!repo_) {
        return;
    }
    git_oid from{};
    git_oid to{};
    if (git_oid_fromstr(&from, fromHex.toLatin1().constData()) < 0 ||
        git_oid_fromstr(&to, toHex.toLatin1().constData()) < 0) {
        return;
    }
    try {
        emit fileDiffReady(
            std::make_shared<const gity::git::FileDiff>(
                gity::git::loadFileDiffBetween(repo_.get(), from, to, path.toStdString())),
            generation);
    } catch (const gity::git::GitException&) {
        // As loadFileDiff: the pane shows nothing, the next selection retries.
    }
}

bool RepoWorker::operationInProgress() const {
    const char* path = repo_ ? git_repository_path(repo_.get()) : nullptr;
    if (path == nullptr) {
        return false;
    }
    const gity::git::Operation operation = gity::git::detectOperation(path);
    return operation != gity::git::Operation::None && operation != gity::git::Operation::Bisect;
}

bool RepoWorker::worktreeClean() const {
    if (!repo_) {
        return true;
    }
    try {
        return gity::git::loadStatus(repo_.get()).entries.empty();
    } catch (const gity::git::GitException&) {
        return false; // unknown: take the full snapshot rather than risk a thin one
    }
}

RepoState RepoWorker::beginUndo() {
    if (!repo_) {
        return {};
    }
    RepoState state = undo_.capture(true, worktreeClean());
    state.duringOperation = operationInProgress();
    return state;
}

void RepoWorker::endUndo(const QString& verb, RepoState before, bool filesChanged) {
    if (!repo_ || (before.headOid.isEmpty() && before.refs.empty())) {
        return;
    }
    // Abandoning something Gity started closes that entry (UndoLog::record).
    // Abandoning something it did not start is recorded as an entry of its
    // own: `merge --abort` throws away conflict resolutions in progress, and
    // the snapshot taken before it is the only copy of them.
    undo_.record(verb, std::move(before), filesChanged, operationInProgress());
    reportUndo();
}

void RepoWorker::reportUndo() {
    emit undoStateChanged(undo_.undoLabel(), undo_.redoLabel());
}

void RepoWorker::undo() {
    undo_.settle(operationInProgress());
    const UndoLog::Outcome outcome = undo_.undo(operationInProgress(), worktreeClean());
    emit historyEditFinished(QObject::tr("Undo"), outcome.ok, false, outcome.message);
    reportUndo();
    reportOperation();
    refreshStatus();
}

void RepoWorker::redo() {
    undo_.settle(operationInProgress());
    const UndoLog::Outcome outcome = undo_.redo(operationInProgress(), worktreeClean());
    emit historyEditFinished(QObject::tr("Redo"), outcome.ok, false, outcome.message);
    reportUndo();
    reportOperation();
    refreshStatus();
}

QString RepoWorker::gityDir() const {
    const char* path = repo_ ? git_repository_path(repo_.get()) : nullptr;
    return path != nullptr ? QDir(QString::fromUtf8(path)).filePath(QStringLiteral("gity"))
                           : QString();
}

} // namespace gity::session
