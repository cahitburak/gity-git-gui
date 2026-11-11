// MainWindow: the menus, the toolbar and the status bar.
#include "MainWindow.h"

#include "SavedState.h"

#include "session/Accounts.h"
#include "session/GitProcess.h"
#include "ui/panels/Confirm.h"
#include "ui/panels/RefsSidebar.h"
#include "ui/panels/RemotesDialog.h"
#include "ui/panels/WelcomePanel.h"
#include "ui/theme/Theme.h"
#include "ui/theme/Tokens.h"
#include "ui/theme/ToolbarIcons.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <git2.h>

namespace gity::app {

namespace tokens = gity::ui::tokens;

void MainWindow::buildMenus() {
    // ADR-002 — native menus are one of the reasons we are on Widgets.
    // Order follows SPEC.md.
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Open Repository…"), QKeySequence::Open, this,
                    &MainWindow::chooseRepository);
    // Rebuilt each time it opens, so it is never stale and a repository that
    // has since gone shows as unavailable rather than failing when chosen.
    QMenu* recent = file->addMenu(tr("Open &Recent"));
    connect(recent, &QMenu::aboutToShow, this, [this, recent] {
        recent->clear();
        const QStringList paths = saved::recentRepositories();
        for (const QString& path : paths) {
            QAction* action =
                recent->addAction(QStringLiteral("%1  —  %2")
                                      .arg(QDir(path).dirName(), QDir::toNativeSeparators(path)),
                                  this, [this, path] { openRepository(path); });
            action->setEnabled(QFileInfo::exists(path));
        }
        if (paths.isEmpty()) {
            recent->addAction(tr("No recent repositories"))->setEnabled(false);
            return;
        }
        recent->addSeparator();
        recent->addAction(tr("Clear Recent"), this, [this] {
            if (persistSession_) {
                saved::clearRecentRepositories();
            }
            welcome_->setRecent({});
        });
    });
    file->addAction(tr("&Clone Repository…"), this, &MainWindow::chooseClone);
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    // Undo for what was done to the repository — a rebase, a reset, a deleted
    // branch — not for typing: a text field keeps Ctrl+Z for itself while it
    // has focus. Labelled with what it will undo, so it is never a guess.
    undoAction_ = edit->addAction(tr("&Undo"), QKeySequence::Undo, this,
                                  [this] { session()->requestUndo(); });
    redoAction_ = edit->addAction(tr("&Redo"), QKeySequence::Redo, this,
                                  [this] { session()->requestRedo(); });
    undoAction_->setEnabled(false);
    redoAction_->setEnabled(false);
    edit->addSeparator();
    edit->addAction(tr("&Find Branch…"), QKeySequence::Find, this, [this] {
        if (refFilter_ != nullptr) {
            refFilter_->setFocus();
            refFilter_->selectAll();
        }
    });

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addAction(tr("&All Commits"), QKeySequence(Qt::CTRL | Qt::Key_1), this,
                    [this] { showScreen(gity::ui::RefsSidebar::Screen::History); });
    view->addAction(tr("&Local Changes"), QKeySequence(Qt::CTRL | Qt::Key_2), this,
                    [this] { showScreen(gity::ui::RefsSidebar::Screen::WorkingCopy); });
    view->addSeparator();
    QAction* showLog = commandLog_->toggleViewAction();
    showLog->setText(tr("&Command Log"));
    showLog->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft));
    view->addAction(showLog);
    view->addSeparator();

    // Theme and row density, switchable without opening Settings. The ticks
    // are refreshed each time the menu opens, so a change made in Settings
    // shows here too.
    QMenu* themeMenu = view->addMenu(tr("&Theme"));
    auto* themes = new QActionGroup(themeMenu);
    for (const gity::ui::Theme::Variant variant : gity::ui::Theme::choices()) {
        QAction* choice = themeMenu->addAction(gity::ui::Theme::displayName(variant));
        choice->setCheckable(true);
        choice->setData(static_cast<int>(variant));
        themes->addAction(choice);
        connect(choice, &QAction::triggered, this, [variant] {
            gity::ui::Theme::setVariant(variant,
                                        qobject_cast<QApplication*>(QCoreApplication::instance()));
        });
        if (variant == gity::ui::Theme::Variant::System) {
            themeMenu->addSeparator();
        }
    }
    QMenu* rowsMenu = view->addMenu(tr("History &Rows"));
    auto* rows = new QActionGroup(rowsMenu);
    QAction* comfortable = rowsMenu->addAction(tr("Comfortable"));
    QAction* compact = rowsMenu->addAction(tr("Compact"));
    for (QAction* density : {comfortable, compact}) {
        density->setCheckable(true);
        rows->addAction(density);
        const bool dense = density == compact;
        connect(density, &QAction::triggered, this, [dense] {
            gity::ui::Theme::setCompact(dense,
                                        qobject_cast<QApplication*>(QCoreApplication::instance()));
        });
    }
    connect(view, &QMenu::aboutToShow, this, [themes, comfortable, compact] {
        const int current = static_cast<int>(gity::ui::Theme::variant());
        for (QAction* choice : themes->actions()) {
            choice->setChecked(choice->data().toInt() == current);
        }
        (gity::ui::Theme::compact() ? compact : comfortable)->setChecked(true);
    });

    QMenu* repository = menuBar()->addMenu(tr("&Repository"));
    repository->addAction(tr("&Remotes…"), this, [this] {
        if (currentWorkdir_.isEmpty()) {
            return;
        }
        gity::ui::RemotesDialog dialog(*session(), this);
        dialog.exec();
        if (dialog.changed()) {
            // Removing a remote takes its tracking refs with it, so the
            // sidebar has to be rebuilt rather than left showing branches that
            // no longer resolve.
            openRepository(currentWorkdir_);
        }
    });
    repository->addSeparator();
    // Lambdas, not the slot itself: triggered(bool checked) would arrive as
    // withSubmodules = false.
    networkMenuActions_ = {
        repository->addAction(tr("&Fetch with Submodules"), QKeySequence(Qt::CTRL | Qt::Key_R),
                              this, [this] { session()->requestFetch(true); }),
        repository->addAction(tr("Fetch This Repository Only"), this,
                              [this] { session()->requestFetch(false); }),
        repository->addAction(tr("&Pull"), this,
                              [this] { askPreview(gity::session::Preview::Kind::Pull); }),
        repository->addAction(tr("Pu&sh"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), this,
                              [this] { askPreview(gity::session::Preview::Kind::Push); }),
    };
    repository->addSeparator();
    repository->addAction(tr("Refresh &LFS Locks"), this, [this] { session()->requestLocks(); });

    QMenu* branch = menuBar()->addMenu(tr("&Branch"));
    branch->addAction(tr("&New Branch…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), this,
                      &MainWindow::chooseNewBranch);
    branch->addAction(tr("&Merge…"), this, &MainWindow::chooseMerge);
    // The same comparison as Ctrl-clicking two branches in the sidebar, for
    // anyone who does not know the gesture (2026-09 UI review).
    branch->addAction(tr("&Compare Branches…"), this, &MainWindow::chooseComparison);
    branch->addSeparator();
    branch->addAction(tr("&Rebase onto Branch…"), this, &MainWindow::chooseRebase);
    branch->addAction(tr("&Interactive Rebase…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R),
                      this, &MainWindow::chooseInteractiveRebase);

    QMenu* stash = menuBar()->addMenu(tr("&Stash"));
    stash->addAction(tr("&Stash Changes…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), this,
                     &MainWindow::chooseStash);

    // Everything in these menus acts on the repository on screen, so none of
    // it — shortcuts included — is live while there is none. The toolbar's
    // twins were disabled; these were not, and Ctrl+R during a fetch queued
    // a second one.
    for (QMenu* menu : {repository, branch, stash}) {
        for (QAction* action : menu->actions()) {
            if (!action->isSeparator()) {
                repositoryMenuActions_ << action;
            }
        }
    }

    edit->addSeparator();
    edit->addAction(tr("&Settings…"), QKeySequence::Preferences, this, [this] { openSettings(); });

    QMenu* tools = menuBar()->addMenu(tr("&Tools"));
    tools->addAction(tr("&Accounts…"), this, [this] { openSettings(SettingsPage::Accounts); });
    tools->addAction(tr("&Credentials…"), this,
                     [this] { openSettings(SettingsPage::Credentials); });
    tools->addAction(tr("Open Repository &Folder"), this, [this] {
        if (!currentWorkdir_.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(currentWorkdir_));
        }
    });

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About Gity"), this, [this] {
        // The linked library rather than the header macro; they can differ.
        int libgit2Major = 0;
        int libgit2Minor = 0;
        int libgit2Patch = 0;
        git_libgit2_version(&libgit2Major, &libgit2Minor, &libgit2Patch);
        QMessageBox::about(
            this, tr("About Gity"),
            tr("<b>Gity %6</b><br>A Git client."
               "<br><br>git %1 · libgit2 %2.%3.%4<br>Qt %5"
               "<br><br>MIT licence. Qt is used under the LGPL v3 and libgit2 under the GPL v2 "
               "with a linking exception; see THIRD_PARTY_NOTICES.md.")
                .arg(gity::session::GitProcess::version().toHtmlEscaped())
                .arg(libgit2Major)
                .arg(libgit2Minor)
                .arg(libgit2Patch)
                .arg(QStringLiteral(QT_VERSION_STR))
                .arg(QCoreApplication::applicationVersion().toHtmlEscaped()));
    });
}

void MainWindow::buildToolBar() {
    auto* bar = addToolBar(tr("Main"));
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->setIconSize(QSize(18, 18));
    // Icon above label, and taller than SPEC.md's 46px: these are the verbs
    // reached most often, and a bigger target for them is worth the pixels on
    // a window that is otherwise dense.
    bar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    bar->setMinimumHeight(tokens::chromeToolbar + 16);

    // Every button here is live as of M6. The two that start disabled do so
    // because they need an open repository, not because they are unfinished.
    using gity::ui::icons::Glyph;
    fetchAction_ = bar->addAction(gity::ui::icons::themed(Glyph::Fetch), tr("Fetch"));
    pullAction_ = bar->addAction(gity::ui::icons::themed(Glyph::Pull), tr("Pull"));
    pushAction_ = bar->addAction(gity::ui::icons::themed(Glyph::Push), tr("Push"));
    connect(fetchAction_, &QAction::triggered, this, [this] { session()->requestFetch(true); });
    // As Rebase has: the everyday verb on the button, the narrower one under
    // its arrow.
    auto* fetchMenu = new QMenu(this);
    QAction* fetchEverything = fetchMenu->addAction(tr("Fetch with Submodules"), this,
                                                    [this] { session()->requestFetch(true); });
    // Bold in the menu: what the button itself does.
    fetchMenu->setDefaultAction(fetchEverything);
    QAction* fetchThisOnly = fetchMenu->addAction(tr("Fetch This Repository Only"), this,
                                                  [this] { session()->requestFetch(false); });
    fetchMenu->setToolTipsVisible(true);
    gity::ui::describeCommand(fetchEverything,
                              tr("Every remote of this repository and of each submodule."),
                              QStringLiteral("fetch --all --prune  ·  in each submodule too"));
    gity::ui::describeCommand(fetchThisOnly,
                              tr("Every remote of this repository; submodules are left alone."),
                              QStringLiteral("fetch --all --prune --recurse-submodules=no"));
    fetchAction_->setMenu(fetchMenu);
    // Pull and Push say what they will do before doing it: the preview is
    // the first step of both, and the command runs from its dialog.
    connect(pullAction_, &QAction::triggered, this,
            [this] { askPreview(gity::session::Preview::Kind::Pull); });
    connect(pushAction_, &QAction::triggered, this,
            [this] { askPreview(gity::session::Preview::Kind::Push); });
    for (QAction* action : {fetchAction_, pullAction_, pushAction_}) {
        action->setEnabled(false); // until a repository is open
    }

    bar->addSeparator();
    branchAction_ = bar->addAction(gity::ui::icons::themed(Glyph::Branch), tr("Branch"));
    branchAction_->setEnabled(false);
    connect(branchAction_, &QAction::triggered, this, &MainWindow::chooseNewBranch);
    stashAction_ = bar->addAction(gity::ui::icons::themed(Glyph::Stash), tr("Stash"));
    stashAction_->setEnabled(false);
    connect(stashAction_, &QAction::triggered, this, &MainWindow::chooseStash);
    mergeAction_ = bar->addAction(gity::ui::icons::themed(Glyph::Merge), tr("Merge"));
    mergeAction_->setEnabled(false);
    connect(mergeAction_, &QAction::triggered, this, &MainWindow::chooseMerge);
    rebaseAction_ = bar->addAction(gity::ui::icons::themed(Glyph::Rebase), tr("Rebase"));
    rebaseAction_->setEnabled(false);
    using gity::ui::describeCommand;
    describeCommand(fetchAction_,
                    tr("Download new commits from every remote, submodules' "
                       "included. Nothing local changes."),
                    QStringLiteral("fetch --all --prune  ·  in each submodule too"));
    describeCommand(pullAction_,
                    tr("Bring the current branch up to date with its upstream — only when "
                       "that needs no merge."),
                    QStringLiteral("pull --ff-only"));
    describeCommand(pushAction_, tr("Send your new commits to the remote."),
                    QStringLiteral("push"));
    describeCommand(branchAction_, tr("Start a new branch here and switch to it."),
                    QStringLiteral("switch -c <name>"));
    describeCommand(stashAction_, tr("Put your uncommitted changes aside for later."),
                    QStringLiteral("stash push --include-untracked"));
    describeCommand(mergeAction_, tr("Bring another branch's work into this one."),
                    QStringLiteral("merge <branch>"));
    describeCommand(rebaseAction_,
                    tr("Replay this branch's commits on top of another branch, or reorder and "
                       "squash them."),
                    QStringLiteral("rebase <branch>  ·  rebase -i <commit>"));
    // A menu, not a single action: there are two rebases and both need to be
    // reachable from the toolbar. Discovering interactive rebase only by
    // right-clicking a commit is how it came to be reported as missing.
    auto* rebaseMenu = new QMenu(this);
    rebaseMenu->addAction(tr("Rebase onto Branch…"), this, &MainWindow::chooseRebase);
    rebaseMenu->addAction(tr("Interactive Rebase…"), this, &MainWindow::chooseInteractiveRebase);
    rebaseAction_->setMenu(rebaseMenu);
    connect(rebaseAction_, &QAction::triggered, this, &MainWindow::chooseRebase);

    // Settings at the far right, as a gear: out of the way of the git verbs,
    // but one click from anywhere — themes included, which no menu entry
    // reached directly. Enabled with no repository open, unlike the verbs.
    auto* spacer = new QWidget(bar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    spacer->setAttribute(Qt::WA_TransparentForMouseEvents);
    bar->addWidget(spacer);
    QAction* settings = bar->addAction(gity::ui::icons::themed(Glyph::Settings), tr("Settings"));
    settings->setToolTip(
        tr("Settings — accounts, credentials, appearance (%1)")
            .arg(QKeySequence(QKeySequence::Preferences).toString(QKeySequence::NativeText)));
    connect(settings, &QAction::triggered, this, [this] { openSettings(SettingsPage::General); });
}

void MainWindow::buildStatusBar() {
    repoLabel_ = new QLabel(tr("No repository open"), this);
    countLabel_ = new QLabel(this);
    // Versions and load timings used to sit here too. They are diagnostics,
    // not status: git and libgit2 versions are in Help ▸ About and Settings ▸
    // About, and timings in gity-bench (2026-09 UI review).

    statusBar()->addWidget(repoLabel_, 1);

    // ADR-003 promises that a network command can be cancelled. Only network
    // commands: a commit or a rebase stopped half way leaves a state nobody
    // chose, so those always run to the end.
    cancelNetwork_ = new QToolButton(this);
    cancelNetwork_->setText(tr("Cancel"));
    cancelNetwork_->setToolTip(tr("Stop the fetch, pull, push or clone running now. Nothing "
                                  "it had not finished is kept."));
    cancelNetwork_->setAutoRaise(true);
    cancelNetwork_->setVisible(false);
    connect(cancelNetwork_, &QToolButton::clicked, this, [this] {
        session()->cancelNetwork();
        statusBar()->showMessage(tr("Cancelling…"));
    });
    statusBar()->addPermanentWidget(cancelNetwork_);
    // The drawer's switch lives where the outcome of the last command is
    // already being reported.
    // The account this repository's remote is reached as — shown, not chosen
    // here: a click opens Settings ▸ Credentials, where it is assigned.
    accountStatus_ = new QToolButton(this);
    accountStatus_->setAutoRaise(true);
    accountStatus_->setVisible(false);
    connect(accountStatus_, &QToolButton::clicked, this,
            [this] { openSettings(SettingsPage::Credentials); });
    connect(sessions_.relay(), &gity::session::RepoSession::accountChoiceReady, this,
            [this](const QString& host, bool viaGitHubCli, const QStringList& accounts,
                   const QString& defaultAccount, const QString& chosen) {
                account_ = {host, viaGitHubCli, accounts, defaultAccount, chosen};
                const QString who = !chosen.isEmpty() ? chosen : defaultAccount;
                accountStatus_->setVisible(!host.isEmpty() && !who.isEmpty());
                accountStatus_->setText(QStringLiteral("%1: %2").arg(host, who));
                accountStatus_->setToolTip(
                    !chosen.isEmpty()
                        ? tr("This repository uses %1 on %2 — set for it alone. Click to change.")
                              .arg(chosen, host)
                        : tr("This repository uses the default account on %1, %2. Click to "
                             "assign one to this repository.")
                              .arg(host, defaultAccount));
            });
    statusBar()->addPermanentWidget(accountStatus_);

    auto* logButton = new QToolButton(this);
    logButton->setDefaultAction(commandLog_->toggleViewAction());
    logButton->setText(tr("Commands"));
    logButton->setToolTip(tr("Show every git command Gity runs (Ctrl+`)"));
    logButton->setAutoRaise(true);
    statusBar()->addPermanentWidget(logButton);
    statusBar()->addPermanentWidget(countLabel_);
}

} // namespace gity::app
