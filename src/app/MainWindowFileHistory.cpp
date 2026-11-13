// MainWindow: the history of one file or folder, and who last changed each
// line of a file.
#include "MainWindow.h"

#include "ui/graphview/CommitGraphView.h"
#include "ui/graphview/HistoryFilterBar.h"
#include "ui/panels/BlamePanel.h"
#include "ui/panels/CommitDetailPanel.h"
#include "ui/panels/WorkingCopyPanel.h"

#include <QDir>
#include <QFileDialog>
#include <QStatusBar>
#include <QTabWidget>

namespace gity::app {

void MainWindow::connectFileHistory() {
    // A commit's files, in either tab: their history, and their lines as of
    // that commit — or, comparing two branches, as of the second.
    const auto blameHere = [this](const QString& path) {
        const QString at = !compareTo_.isEmpty() ? compareTo_ : currentOid_;
        if (!at.isEmpty()) {
            blameFile(path, at);
        }
    };
    connect(detail_, &gity::ui::CommitDetailPanel::historyRequested, this,
            &MainWindow::showFileHistory);
    connect(detail_, &gity::ui::CommitDetailPanel::blameRequested, this, blameHere);
    connect(commitFiles_, &gity::ui::ChangedFilesList::historyRequested, this,
            &MainWindow::showFileHistory);
    connect(commitFiles_, &gity::ui::ChangedFilesList::blameRequested, this, blameHere);

    // The working copy's files: their lines as they are on disk now.
    connect(changes_, &gity::ui::WorkingCopyPanel::historyRequested, this,
            &MainWindow::showFileHistory);
    connect(changes_, &gity::ui::WorkingCopyPanel::blameRequested, this,
            [this](const QString& path) { blameFile(path, {}); });

    connect(blame_, &gity::ui::BlamePanel::blameRequested, this,
            [this](const QString& path, const QString& revision) {
                session()->requestBlame(path, revision);
            });
    connect(sessions_.relay(), &gity::session::RepoSession::blameReady, blame_,
            &gity::ui::BlamePanel::setBlame);
    connect(blame_, &gity::ui::BlamePanel::commitActivated, this, &MainWindow::selectCommit);
}

void MainWindow::showFileHistory(const QString& path) {
    if (currentWorkdir_.isEmpty() || path.isEmpty()) {
        return;
    }
    showScreen(gity::ui::RefsSidebar::Screen::History);
    // The commit being read stays selected if it changed the file, which
    // keeps the place; otherwise the newest that did is selected once the
    // walk ends.
    pendingSelectOid_ = currentOid_;
    firstChunkSeen_ = false;
    filterBar_->showPath(path);
}

void MainWindow::blameFile(const QString& path, const QString& revision) {
    if (currentWorkdir_.isEmpty() || path.isEmpty()) {
        return;
    }
    showScreen(gity::ui::RefsSidebar::Screen::History);
    commitTabs_->setCurrentIndex(kBlameTab);
    blame_->start(path, revision);
}

QString MainWindow::choosePath(bool folder) {
    if (currentWorkdir_.isEmpty()) {
        return {};
    }
    const QString chosen =
        folder ? QFileDialog::getExistingDirectory(this, tr("Folder History"), currentWorkdir_)
               : QFileDialog::getOpenFileName(this, tr("Choose a File"), currentWorkdir_);
    if (chosen.isEmpty()) {
        return {};
    }
    const QString relative = QDir(currentWorkdir_).relativeFilePath(chosen);
    if (relative.startsWith(QStringLiteral("..")) || QDir::isAbsolutePath(relative)) {
        statusBar()->showMessage(tr("%1 is not in this repository.").arg(chosen), 6000);
        return {};
    }
    // The top folder itself is the whole history.
    return relative == QStringLiteral(".") ? QString() : QDir::fromNativeSeparators(relative);
}

void MainWindow::selectCommit(const QString& id) {
    const qsizetype row = model_.rowForId(id);
    if (row >= 0) {
        graph_->selectRow(row);
        return;
    }
    statusBar()->showMessage(
        walkedFilter_.active()
            ? tr("%1 is not among the commits shown. Clear the filters to find it.").arg(id.left(7))
            : tr("%1 is not in the history loaded yet.").arg(id.left(7)),
        6000);
}

} // namespace gity::app
