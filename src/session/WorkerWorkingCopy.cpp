// RepoWorker: the working copy — status, staging, discarding, image
// diffs and committing.
#include "RepoWorker.h"

#include "IndexWrites.h"
#include "RepoWatcher.h"
#include "WorkdirPaths.h"

#include <QDir>
#include <QFile>

namespace gity::session {

void RepoWorker::refreshStatus() {
    if (!repo_) {
        return;
    }
    // Whatever changed up to now is in the status about to be read; only
    // what changes after it is news.
    if (watcher_ != nullptr) {
        watcher_->absorb();
    }
    try {
        emit statusReady(std::make_shared<const gity::git::WorkingCopyStatus>(
            gity::git::loadStatus(repo_.get())));
    } catch (const gity::git::GitException& e) {
        // Not silent. A status that cannot be read leaves the working-copy
        // screen showing what was there before, and without this the only
        // symptom is a screen that quietly stops updating.
        qWarning("gity: could not read the working copy status: %s", e.error().message.c_str());
    }
    // Whether a merge or rebase is underway changes for the same reasons the
    // status does — a commit concluding it, an abort, a resolution. Reporting
    // it here means every path that refreshes status keeps the banner honest,
    // rather than each caller having to remember.
    reportOperation();
}

void RepoWorker::stage(const QStringList& paths) {
    if (!repo_) {
        return;
    }
    reportIndexWrite(index::stagePaths(workdir_, paths));
}

void RepoWorker::unstage(const QStringList& paths) {
    if (!repo_) {
        return;
    }
    reportIndexWrite(index::unstagePaths(workdir_, paths));
}

void RepoWorker::reportIndexWrite(const GitResult& result) {
    if (!result.ok()) {
        emit commitFailed(result.firstProblemLine(), result.errorOutput);
    }
    // Refreshed whether it worked or not: a write that half happened — some
    // paths staged before a filter failed on the next — has to be shown as it
    // is, not as it was.
    refreshStatus();
}

void RepoWorker::loadWorkingFileDiff(const QString& path, bool staged, quint64 generation) {
    if (!repo_) {
        return;
    }
    try {
        auto diff = std::make_shared<const gity::git::FileDiff>(gity::git::loadWorkingDiff(
            repo_.get(), path.toStdString(),
            staged ? gity::git::DiffSide::Staged : gity::git::DiffSide::Unstaged));

        // Only the unstaged side can be partly staged. On the staged side
        // every line shown is staged already, so the count would say nothing.
        gity::session::HunkStagingPtr staging;
        if (!staged && !diff->hunks.empty()) {
            // Counted, never shown: the files themselves are not needed.
            gity::git::FileDiffOptions countOnly;
            countOnly.readSources = false;
            const gity::git::FileDiff combined = gity::git::loadWorkingDiff(
                repo_.get(), path.toStdString(), gity::git::DiffSide::Combined, countOnly);
            staging = std::make_shared<const std::vector<gity::git::HunkStaging>>(
                gity::git::hunkStagingCounts(*diff, combined));
        }
        emit workingDiffReady(std::move(diff), std::move(staging), generation);
    } catch (const gity::git::GitException& e) {
        qWarning("gity: could not diff %s: %s", qPrintable(path), e.error().message.c_str());
    }
}

void RepoWorker::applyPatch(const QByteArray& patch) {
    if (!repo_ || patch.isEmpty()) {
        return;
    }
    reportIndexWrite(index::applyToIndex(workdir_, patch));
}

void RepoWorker::discardPatch(const QByteArray& patch) {
    if (!repo_ || patch.isEmpty()) {
        return;
    }
    RepoState undoBefore = beginUndo();
    const GitResult result = index::applyToWorktree(workdir_, patch);
    if (result.ok()) {
        endUndo(QObject::tr("Discard hunk"), std::move(undoBefore), true);
    }
    reportIndexWrite(result);
}

void RepoWorker::discardPaths(const QStringList& paths) {
    if (!repo_ || paths.isEmpty()) {
        return;
    }

    QStringList expanded = paths;
    expanded.removeDuplicates();

    // Tracked and untracked need different verbs: restore returns a tracked
    // file to the commit, but an untracked one has no committed state to
    // return to and can only be deleted.
    QStringList tracked;
    QStringList untracked;
    try {
        // Once, not once per path: a status of a large repository costs
        // about a second, and this runs on every discard.
        const gity::git::WorkingCopyStatus status = gity::git::loadStatus(repo_.get());
        for (const QString& path : expanded) {
            const gity::git::StatusEntry* entry = status.find(path.toStdString());
            if (entry == nullptr) {
                tracked << path;
                continue;
            }
            // A rename not yet staged is, to git, a deleted file and a new
            // one it has never seen: the new path is removed like any
            // untracked file and the old one restored. Handing `restore` the
            // new path made it refuse the whole list.
            const bool unknownToGit =
                entry->isUntracked() ||
                (entry->unstaged == gity::git::FileState::Renamed && !entry->hasStaged());
            (unknownToGit ? untracked : tracked) << path;
            if (!entry->oldPath.empty()) {
                tracked << QString::fromStdString(entry->oldPath);
            }
        }
        tracked.removeDuplicates();
    } catch (const gity::git::GitException&) {
        // Without a status every path is treated as tracked; `restore` will
        // say so itself for anything that is not.
        tracked = expanded;
    }

    RepoState undoBefore = beginUndo();
    if (!tracked.isEmpty()) {
        // --literal-pathspecs: these are file names, not patterns. Read as a
        // glob, discarding `[id].tsx` also reverts `d.tsx` — measured, and
        // with nothing to undo it.
        //
        // Before the first commit there is no HEAD to restore from, and
        // `restore --source=HEAD` refuses outright. Everything staged then
        // is new, and returning it to "the last commit" means removing it
        // from the index and the disk, which is what `rm -f` does.
        const bool unborn = git_repository_head_unborn(repo_.get()) == 1;
        QStringList args{QStringLiteral("--literal-pathspecs")};
        if (unborn) {
            args << QStringLiteral("rm") << QStringLiteral("-q") << QStringLiteral("-f")
                 << QStringLiteral("--ignore-unmatch");
        } else {
            args << QStringLiteral("restore") << QStringLiteral("--source=HEAD")
                 << QStringLiteral("--staged") << QStringLiteral("--worktree");
        }
        args << QStringLiteral("--");
        args += tracked;
        const GitResult result = GitProcess::run(workdir_, args, 120000);
        if (!result.ok()) {
            // Recorded even so: git may have restored some paths before it
            // failed on one, and the snapshot is the only copy of them.
            endUndo(QObject::tr("Discard"), std::move(undoBefore), true);
            emit historyEditFinished(QObject::tr("Discard"), false, false,
                                     result.firstProblemLine());
            refreshStatus();
            return;
        }
    }

    // Reported, not ignored: a file that could not be deleted is still there,
    // and saying "discarded" about it would be false. A directory is never
    // removed wholesale — status lists one only when it holds a repository
    // of its own, whose history `git clean` also refuses to delete.
    //
    // Deleted by Gity rather than git, so held to the rule git keeps: nothing
    // through a link out of the working copy, nothing inside .git. The paths
    // come from the status, which keeps to that already; this is the check
    // at the point of deleting.
    QStringList stuck;
    for (const QString& path : untracked) {
        if (!deletableInWorkdir(workdir_, path) || !QFile::remove(QDir(workdir_).filePath(path))) {
            stuck << path;
        }
    }
    if (!stuck.isEmpty()) {
        // The tracked files are already restored, and the other untracked
        // ones deleted: that much has happened and must be undoable.
        endUndo(QObject::tr("Discard"), std::move(undoBefore), true);
        emit historyEditFinished(QObject::tr("Discard"), false, false,
                                 QObject::tr("Could not delete %1").arg(stuck.join(", ")));
        refreshStatus();
        return;
    }

    endUndo(QObject::tr("Discard"), std::move(undoBefore), true);
    emit historyEditFinished(
        QObject::tr("Discard"), true, false,
        QObject::tr("Discarded changes to %n file(s)", nullptr, static_cast<int>(expanded.size())));
    refreshStatus();
}

namespace {

QString humanSize(qint64 bytes) {
    const double value = static_cast<double>(bytes);
    if (bytes >= 1024LL * 1024) {
        return QStringLiteral("%1 MB").arg(value / (1024.0 * 1024.0), 0, 'f', 1);
    }
    if (bytes >= 1024) {
        return QStringLiteral("%1 KB").arg(value / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

/// Fills one side from bytes we already have. Reports what the file actually
/// says; anything needing a decoder we do not have is left out rather than guessed.
void fillSide(ImageSide& side, const gity::git::BlobContent& content, const QString& label) {
    side.label = label;
    side.present = !content.empty();
    side.lfsPointer = content.lfsPointer;
    side.lfsOid = QString::fromStdString(content.lfsOid);
    side.byteSize = content.lfsPointer ? static_cast<qint64>(content.lfsSize)
                                       : static_cast<qint64>(content.size());

    if (!side.present) {
        side.metadata = QObject::tr("not present");
        return;
    }
    if (content.lfsPointer) {
        // The pointer is all we have without `git lfs pull`, so say that
        // rather than rendering 130 bytes of text as a broken image.
        side.metadata =
            QObject::tr("LFS pointer · %1 · object not fetched").arg(humanSize(side.byteSize));
        return;
    }

    const QByteArray raw(content.bytes.data(), static_cast<qsizetype>(content.size()));
    side.decoded = side.image.loadFromData(raw);
    if (!side.decoded) {
        side.metadata = QObject::tr("%1 · not a decodable image").arg(humanSize(side.byteSize));
        return;
    }
    side.metadata = QObject::tr("%1 × %2 · %3-bit · %4")
                        .arg(side.image.width())
                        .arg(side.image.height())
                        .arg(side.image.depth())
                        .arg(humanSize(side.byteSize));
}

double changedPercent(const QImage& a, const QImage& b) {
    if (a.size() != b.size() || a.isNull()) {
        return 0.0;
    }
    // Sampled rather than exhaustive: a 4096² pair is 16M pixels and the
    // caption only needs one decimal place.
    const int step = std::max(1, a.width() / 512);
    qint64 sampled = 0;
    qint64 differing = 0;
    for (int y = 0; y < a.height(); y += step) {
        for (int x = 0; x < a.width(); x += step) {
            ++sampled;
            if (a.pixel(x, y) != b.pixel(x, y)) {
                ++differing;
            }
        }
    }
    return sampled == 0 ? 0.0
                        : (100.0 * static_cast<double>(differing) / static_cast<double>(sampled));
}

} // namespace

void RepoWorker::loadImageDiff(const QString& path, const QString& commitOid,
                               const QString& baseOid, quint64 generation) {
    if (!repo_) {
        return;
    }
    try {
        auto diff = std::make_shared<ImageDiff>();
        diff->path = path;

        const std::string native = path.toStdString();
        git_oid commit{};
        if (!commitOid.isEmpty() &&
            git_oid_fromstr(&commit, commitOid.toLatin1().constData()) == 0) {
            // What this commit did to the file: its first parent against it.
            // Reading HEAD and the working copy here showed an old commit's
            // image as whatever it looks like today.
            gity::git::BlobContent before;
            QString beforeLabel = QObject::tr("Before");
            gity::git::CommitHandle handle;
            git_oid base{};
            if (!baseOid.isEmpty() && git_oid_fromstr(&base, baseOid.toLatin1().constData()) == 0) {
                // A comparison: the other tip, not the parent.
                before = gity::git::readBlobAt(repo_.get(), base, native);
                beforeLabel = baseOid.left(8);
            } else if (git_commit_lookup(handle.receive(), repo_.get(), &commit) == 0 &&
                       git_commit_parentcount(handle.get()) > 0) {
                const git_oid* parent = git_commit_parent_id(handle.get(), 0);
                before = gity::git::readBlobAt(repo_.get(), *parent, native);
                beforeLabel = QString::fromLatin1(git_oid_tostr_s(parent)).left(8);
            }
            fillSide(diff->before, before, beforeLabel);
            fillSide(diff->after, gity::git::readBlobAt(repo_.get(), commit, native),
                     commitOid.left(8));
        } else {
            fillSide(diff->before, gity::git::readBlobAtHead(repo_.get(), native),
                     QObject::tr("HEAD"));
            fillSide(diff->after, gity::git::readWorktreeFile(workdir_.toStdString(), native),
                     QObject::tr("Working copy"));
        }

        if (diff->comparable()) {
            diff->changedPixelPercent = changedPercent(diff->before.image, diff->after.image);
        }
        emit imageDiffReady(diff, generation);
    } catch (const gity::git::GitException&) {
    }
}

void RepoWorker::commit(const QString& message, bool amend) {
    if (!repo_) {
        return;
    }
    // ADR-003: the commit itself goes through the git binary, so hooks,
    // signing, and the user's config behave exactly as in their terminal.
    // The message goes on stdin — never through a command line.
    QStringList args{QStringLiteral("commit"), QStringLiteral("-F"), QStringLiteral("-")};
    if (amend) {
        args << QStringLiteral("--amend");
    }
    // Recommitting a commit being edited mid-rebase: it is still that
    // person's commit, from that day.
    const QStringList editAuthor = amend ? QStringList() : editStopAuthorArgs();
    args << editAuthor;
    RepoState undoBefore = beginUndo();
    const GitResult result = GitProcess::runWithInput(workdir_, args, message.toUtf8());
    if (result.ok() && !editAuthor.isEmpty()) {
        clearEditStop();
        reportOperation();
    }
    endUndo(amend ? QObject::tr("Amend") : QObject::tr("Commit"), std::move(undoBefore),
            result.ok());

    if (result.ok()) {
        const GitResult described =
            GitProcess::run(workdir_, {QStringLiteral("log"), QStringLiteral("-1"),
                                       QStringLiteral("--format=%h %s")});
        emit committed(described.ok() ? described.output.trimmed() : QString());
    } else {
        emit commitFailed(result.firstProblemLine(), result.errorOutput);
    }
}

} // namespace gity::session
