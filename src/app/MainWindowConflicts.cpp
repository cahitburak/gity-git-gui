// MainWindow: the conflict editor — its window, and the ways into it.
#include "MainWindow.h"

#include "ui/conflict/ConflictWindow.h"
#include "ui/panels/OperationBanner.h"
#include "ui/panels/WorkingCopyPanel.h"

#include <QStatusBar>

#include <algorithm>

namespace gity::app {

void MainWindow::connectConflicts() {
    using gity::session::RepoSession;
    using gity::ui::ConflictWindow;

    conflicts_ = new ConflictWindow(this);

    // What the window asks for, done by the session of the repository shown.
    connect(conflicts_, &ConflictWindow::conflictRequested, this,
            [this](const QString& path) { session()->requestConflict(path); });
    connect(conflicts_, &ConflictWindow::saveRequested, this,
            [this](const QString& path, const QByteArray& content, bool markResolved) {
                session()->requestSaveResolution(path, content, markResolved);
            });
    connect(
        conflicts_, &ConflictWindow::sideRequested, this, [this](const QString& path, int side) {
            session()->requestResolveWithSide(path, static_cast<RepoSession::ConflictSide>(side));
        });
    connect(conflicts_, &ConflictWindow::restartRequested, this,
            [this](const QString& path) { session()->requestRestartConflict(path); });
    // The same Continue as the banner's, with the commit box's message.
    connect(conflicts_, &ConflictWindow::continueRequested, this,
            [this] { session()->requestContinueOperation(changes_->draftMessage()); });

    connect(sessions_.relay(), &RepoSession::conflictReady, conflicts_,
            &ConflictWindow::setConflict);
    connect(sessions_.relay(), &RepoSession::conflictWriteFinished, this,
            [this](const QString& path, bool ok, const QString& message) {
                conflicts_->writeFinished(path, ok, message);
                statusBar()->showMessage(message, ok ? 6000 : 0);
            });
    connect(sessions_.relay(), &RepoSession::operationChanged, conflicts_,
            [this](int operation, const QString& noun) {
                conflicts_->setOperation(static_cast<gity::git::Operation>(operation), noun);
            });

    // The ways in: the banner, a conflicted file in Local Changes, and the
    // Repository menu (MainWindowMenus.cpp).
    connect(operationBanner_, &gity::ui::OperationBanner::resolveRequested, this,
            [this] { openConflicts(); });
    connect(changes_, &gity::ui::WorkingCopyPanel::resolveRequested, this,
            &MainWindow::openConflicts);
    connect(changes_, &gity::ui::WorkingCopyPanel::takeSideRequested, this,
            [this](const QString& path, bool ours) {
                session()->requestResolveWithSide(path, ours ? RepoSession::ConflictSide::Ours
                                                             : RepoSession::ConflictSide::Theirs);
            });
}

void MainWindow::openConflicts(const QString& path) {
    if (currentWorkdir_.isEmpty()) {
        return;
    }
    conflicts_->openFile(path);
}

void MainWindow::setConflictLayout(int layout) {
    conflicts_->editor()->setLayoutChoice(
        static_cast<gity::ui::ConflictEditor::Layout>(std::clamp(layout, 0, 1)));
}

QWidget* MainWindow::conflictWindow() const {
    return conflicts_;
}

} // namespace gity::app
