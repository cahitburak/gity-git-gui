// The conflict editor's reads and writes.
//
// Reading is libgit2 (ADR-003): the three versions are blobs in the index.
// Every write goes through git — `add` to mark a file resolved, `checkout
// --ours`/`--theirs` to take a side, `rm` to take a deletion, `checkout -m`
// to start over — except the editor's result itself, which is the file's new
// content and is written to the working copy as any editor would write it.
#include "RepoWorker.h"

#include "IndexWrites.h"
#include "WorkdirPaths.h"
#include "core/git/Conflict.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace gity::session {

void RepoWorker::loadConflict(const QString& path, quint64 generation) {
    if (!repo_) {
        return;
    }
    try {
        auto conflict = std::make_shared<const gity::git::ConflictFile>(
            gity::git::loadConflict(repo_.get(), path.toStdString()));
        emit conflictReady(path, std::move(conflict), {}, generation);
    } catch (const std::exception& error) {
        emit conflictReady(path, nullptr, QString::fromUtf8(error.what()), generation);
    }
}

void RepoWorker::saveResolution(const QString& path, const QByteArray& content, bool markResolved) {
    if (!repo_) {
        return;
    }
    // A path the UI got from the status, checked anyway since it is about to
    // be written to — and written by Gity, not by git, which would refuse a
    // path into .git or through a link out of the working copy itself.
    if (!writableInWorkdir(workdir_, path)) {
        emit conflictWriteFinished(path, false, tr("Not a file in this repository: %1").arg(path));
        return;
    }
    const QString full = QDir(workdir_).filePath(path);
    // The file is replaced whole, not rewritten in place, so a crash half
    // way leaves the old one. Its permissions — an executable script — go
    // with it.
    const QFileInfo before(full);
    const bool existed = before.exists();
    const QFileDevice::Permissions permissions =
        existed ? QFile::permissions(full) : QFileDevice::Permissions{};
    QDir().mkpath(before.absolutePath());
    QSaveFile file(full);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size() ||
        !file.commit()) {
        emit conflictWriteFinished(path, false,
                                   tr("Could not write %1: %2").arg(path, file.errorString()));
        refreshStatus();
        return;
    }
    if (existed) {
        QFile::setPermissions(full, permissions);
    }

    if (markResolved) {
        const GitResult added = index::stagePaths(workdir_, {path});
        if (!added.ok()) {
            emit conflictWriteFinished(path, false, added.firstProblemLine());
            refreshStatus();
            return;
        }
        emit conflictWriteFinished(path, true, tr("Resolved %1").arg(path));
    } else {
        emit conflictWriteFinished(path, true, tr("Saved %1 — not marked resolved yet").arg(path));
    }
    refreshStatus();
}

void RepoWorker::resolveWithSide(const QString& path, int side) {
    if (!repo_) {
        return;
    }
    // --literal-pathspecs: a file name, not a pattern.
    GitResult result;
    QString done;
    if (side == 2) {
        result = GitProcess::run(workdir_, {QStringLiteral("--literal-pathspecs"),
                                            QStringLiteral("rm"), QStringLiteral("-q"),
                                            QStringLiteral("-f"), QStringLiteral("--"), path});
        done = tr("Resolved %1 by deleting it").arg(path);
    } else {
        const bool ours = side == 0;
        result = GitProcess::run(workdir_,
                                 {QStringLiteral("--literal-pathspecs"), QStringLiteral("checkout"),
                                  ours ? QStringLiteral("--ours") : QStringLiteral("--theirs"),
                                  QStringLiteral("--"), path});
        if (result.ok()) {
            result = index::stagePaths(workdir_, {path});
        }
        done = ours ? tr("Resolved %1 with our version").arg(path)
                    : tr("Resolved %1 with their version").arg(path);
    }
    emit conflictWriteFinished(path, result.ok(), result.ok() ? done : result.firstProblemLine());
    refreshStatus();
}

void RepoWorker::restartConflict(const QString& path) {
    if (!repo_) {
        return;
    }
    const GitResult result = GitProcess::run(
        workdir_, {QStringLiteral("--literal-pathspecs"), QStringLiteral("checkout"),
                   QStringLiteral("-m"), QStringLiteral("--"), path});
    emit conflictWriteFinished(path, result.ok(),
                               result.ok() ? tr("%1 is back to how the merge left it").arg(path)
                                           : result.firstProblemLine());
    refreshStatus();
}

} // namespace gity::session
