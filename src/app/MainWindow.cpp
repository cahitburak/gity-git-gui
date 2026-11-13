#include "MainWindow.h"

#include "SavedState.h"

#include "core/git/Provider.h"
#include "core/git/PullRequest.h"
#include "ui/diffview/DiffView.h"
#include "ui/graphview/CommitGraphView.h"
#include "ui/graphview/GraphHeader.h"
#include "ui/graphview/HistoryFilterBar.h"
#include "ui/image/ImageDiffView.h"
#include "ui/panels/BlamePanel.h"
#include "ui/panels/CommandLogView.h"
#include "ui/panels/CommitDetailPanel.h"
#include "ui/panels/Confirm.h"
#include "ui/panels/OperationBanner.h"
#include "ui/panels/RefsSidebar.h"
#include "ui/panels/RepositoryTabs.h"
#include "ui/panels/SwitchDialogs.h"
#include "ui/panels/WelcomePanel.h"
#include "ui/panels/WorkingCopyPanel.h"
#include "ui/theme/Tokens.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QScreen>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace gity::app {
namespace tokens = gity::ui::tokens;

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    buildLayout();
    buildMenus();
    buildToolBar();
    buildStatusBar();
    syncNetworkState(); // nothing is open yet

    connectSession();
    connectHistory();
    connectSidebar();
    connectWorkingCopy();
    connectRepositoryTabs();
    connectFileHistory();
}

void MainWindow::buildLayout() {
    setWindowTitle(tr("Gity"));

    // 1024×700 at the least, so a laptop at 125% scaling can hold it; the
    // history's secondary columns give way below their comfortable width
    // (GraphColumns) and every pane is resizable (2026-09 UI review, which
    // replaced SPEC.md's hard 1440 minimum). The first size fits the screen.
    setMinimumSize(tokens::windowMinWidth, 700);
    QSize initial(tokens::windowWidth, tokens::windowHeight);
    if (const QScreen* screen = QGuiApplication::primaryScreen(); screen != nullptr) {
        initial = initial.boundedTo(screen->availableGeometry().size() * 0.92);
    }
    resize(initial.expandedTo(minimumSize()));

    graph_ = new gity::ui::CommitGraphView(this);
    graph_->setModel(&model_);

    // Header and rows share GraphColumns, which is what keeps the labels over
    // their columns as the window resizes.
    // SPEC.md §1 draws a 25px column header. Removed on the owner's
    // instruction: the columns are self-evident from their contents, and the
    // row buys nothing on the screen people look at most.
    graphHeader_ = new gity::ui::GraphHeader(this);
    graphHeader_->setVisible(false);
    auto* graphPane = new QWidget(this);
    auto* graphLayout = new QVBoxLayout(graphPane);
    graphLayout->setContentsMargins(0, 0, 0, 0);
    graphLayout->setSpacing(0);
    // The history's own header: what is listed, and the filters over it.
    filterBar_ = new gity::ui::HistoryFilterBar(graphPane);
    connect(filterBar_, &gity::ui::HistoryFilterBar::filterChanged, this,
            &MainWindow::applyCommitFilter);
    graphLayout->addWidget(filterBar_);
    graphLayout->addWidget(graphHeader_);
    graphLayout->addWidget(graph_, 1);

    refs_ = new gity::ui::RefsSidebar(this);

    detail_ = new gity::ui::CommitDetailPanel(this);
    diff_ = new gity::ui::DiffView(this);
    commitFiles_ = new gity::ui::ChangedFilesList(this);

    // Under the graph, two tabs: Commit is the record — who, when, the
    // message, what it touched — and Changes is the reading surface, the file
    // list beside its diff. The list is chosen from and the diff read, so the
    // two sit side by side and both stay visible while scrubbing.
    // An image has no readable textual diff, so for one the comparison takes
    // the diff's place — where the eye already is — rather than a screen of its
    // own that walking the file list with the arrow keys would jump to.
    image_ = new gity::ui::ImageDiffView(this);
    diffStack_ = new QStackedWidget(this);
    diffStack_->addWidget(diff_);
    diffStack_->addWidget(image_);

    auto* changesSplit = new QSplitter(Qt::Horizontal, this);
    changesSplit->setObjectName(QStringLiteral("changesSplit"));
    changesSplit->addWidget(commitFiles_);
    changesSplit->addWidget(diffStack_);
    changesSplit->setStretchFactor(0, 1);
    changesSplit->setStretchFactor(1, 3);
    changesSplit->setSizes({280, 800});
    // Small floors, and nothing snaps shut: a pane dragged small stays a
    // pane, rather than stopping at its content's size and then vanishing
    // (owner's request, 2026-09-28).
    commitFiles_->setMinimumWidth(60);
    diffStack_->setMinimumWidth(80);
    changesSplit->setChildrenCollapsible(false);

    commitTabs_ = new QTabWidget(this);
    commitTabs_->setObjectName(QStringLiteral("commitTabs"));
    commitTabs_->setDocumentMode(true);
    commitTabs_->addTab(detail_, tr("Commit"));
    commitTabs_->addTab(changesSplit, tr("Changes"));
    blame_ = new gity::ui::BlamePanel(this);
    commitTabs_->addTab(blame_, tr("Blame"));
    // Changes first: the diff is what is read most, and the full message is
    // one click away on Commit (2026-09 UI review).
    commitTabs_->setCurrentIndex(kChangesTab);

    // A one-line summary of what the tabs describe — the commit, or the two
    // branches compared — so the diff can take the room below it without the
    // Commit tab having to be open to know what is being read.
    auto* summaryBar = new QWidget(this);
    summaryBar->setObjectName(QStringLiteral("commitSummary"));
    summaryBar->setAttribute(Qt::WA_StyledBackground, true);
    auto* summaryLayout = new QVBoxLayout(summaryBar);
    summaryLayout->setContentsMargins(12, 7, 12, 6);
    summaryLayout->setSpacing(1);
    summaryTitle_ = new QLabel(summaryBar);
    summaryTitle_->setObjectName(QStringLiteral("commitSummaryTitle"));
    summaryTitle_->setTextFormat(Qt::PlainText);
    summaryMeta_ = new QLabel(summaryBar);
    summaryMeta_->setTextFormat(Qt::PlainText);
    summaryMeta_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    gity::ui::applyRole(summaryMeta_, QStringLiteral("muted"));
    summaryLayout->addWidget(summaryTitle_);
    summaryLayout->addWidget(summaryMeta_);

    auto* lowerPane = new QWidget(this);
    auto* lowerLayout = new QVBoxLayout(lowerPane);
    lowerLayout->setContentsMargins(0, 0, 0, 0);
    lowerLayout->setSpacing(0);
    lowerLayout->addWidget(summaryBar);
    lowerLayout->addWidget(commitTabs_, 1);

    // Graph above, the tabs below: the graph is the thing being scanned, and
    // the tabs answer a question about whatever the scan landed on.
    auto* rightSplit = new QSplitter(Qt::Vertical, this);
    rightSplit->setObjectName(QStringLiteral("historySplit"));
    rightSplit->addWidget(graphPane);
    rightSplit->addWidget(lowerPane);
    // Nearly even: the lower half is where the diff is read, and a fifth of
    // the height was not enough of it.
    rightSplit->setStretchFactor(0, 1);
    rightSplit->setStretchFactor(1, 1);
    rightSplit->setSizes({380, 400});

    // Either half can be dragged down to its header and no further: the graph
    // to "All commits", the lower half to its summary and the Commit | Changes
    // tabs. Content below is clipped rather than holding the divider back, and
    // the header left showing is how to bring it back — click it.
    rightSplit->setChildrenCollapsible(false);
    graphPane->setMinimumHeight(filterBar_->sizeHint().height());
    lowerPane->setMinimumHeight(summaryBar->sizeHint().height() +
                                commitTabs_->tabBar()->sizeHint().height());
    historySplit_ = rightSplit;
    connect(rightSplit, &QSplitter::splitterMoved, this, [this, graphPane, lowerPane] {
        // Remembered while comfortable, so a click can restore it.
        if (graphPane->height() > graphPane->minimumHeight() + 40 &&
            lowerPane->height() > lowerPane->minimumHeight() + 40) {
            restoreHistorySizes_ = historySplit_->sizes();
        }
    });
    const auto restoreIfMinimised = [this](QWidget* pane) {
        if (pane->height() > pane->minimumHeight() + 40) {
            return;
        }
        QList<int> sizes = restoreHistorySizes_;
        const int total = historySplit_->sizes().value(0) + historySplit_->sizes().value(1);
        if (sizes.size() != 2 || sizes.value(0) + sizes.value(1) <= 0) {
            sizes = {total / 2, total - total / 2};
        }
        historySplit_->setSizes(sizes);
    };
    connect(commitTabs_->tabBar(), &QTabBar::tabBarClicked, this,
            [restoreIfMinimised, lowerPane](int) { restoreIfMinimised(lowerPane); });
    filterBar_->installEventFilter(this);
    historyHeaderRestore_ = [restoreIfMinimised, graphPane] { restoreIfMinimised(graphPane); };

    // Two modes, not two windows: history is what you read, changes is what
    // you act on, and the refs sidebar is meaningful to both.
    changes_ = new gity::ui::WorkingCopyPanel(this);

    // SPEC.md drives screens from the sidebar, not from a tab bar: Working
    // copy is a row in the Workspace group. A stack has no
    // chrome of its own, so the sidebar selection is the only thing saying
    // which screen you are on.
    auto* tabs = new QStackedWidget(this);
    tabs_ = tabs;
    tabs->addWidget(rightSplit);
    tabs->addWidget(changes_);

    // With nothing open, the ways in and the repositories used recently —
    // rather than an empty graph saying to use the File menu.
    welcome_ = new gity::ui::WelcomePanel(this);
    tabs->addWidget(welcome_);
    connect(welcome_, &gity::ui::WelcomePanel::openRequested, this, &MainWindow::chooseRepository);
    connect(welcome_, &gity::ui::WelcomePanel::cloneRequested, this, &MainWindow::chooseClone);
    connect(welcome_, &gity::ui::WelcomePanel::recentActivated, this, &MainWindow::openRepository);
    welcome_->setRecent(saved::recentRepositories());
    tabs->setCurrentIndex(ScreenWelcome);

    // The banner sits above every screen rather than inside one: being
    // mid-merge is true of the repository, not of whichever view happens to be
    // showing, and it must not be possible to navigate away from it.
    operationBanner_ = new gity::ui::OperationBanner(this);
    repoTabs_ = new gity::ui::RepositoryTabs(this);

    auto* right = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(operationBanner_);
    rightLayout->addWidget(tabs, 1);

    // The sidebar plus its filter. A repository with a hundred branches is
    // ordinary and scrolling for one is not a way to find it.
    auto* sidebar = new QWidget(this);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(8, 8, 8, 0);
    sidebarLayout->setSpacing(6);

    refFilter_ = new QLineEdit(sidebar);
    refFilter_->setPlaceholderText(tr("Filter branches…"));
    refFilter_->setClearButtonEnabled(true);
    refFilter_->setObjectName(QStringLiteral("refFilter"));
    connect(refFilter_, &QLineEdit::textChanged, this,
            [this](const QString& text) { refs_->setFilter(text); });
    sidebarLayout->addWidget(refFilter_);
    sidebarLayout->addWidget(refs_, 1);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setObjectName(QStringLiteral("mainSplit"));
    split->addWidget(sidebar);
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    sidebar->setMinimumWidth(120);
    split->setSizes({tokens::panesSidebar, tokens::windowWidth - tokens::panesSidebar});
    split->setChildrenCollapsible(false);
    right->setMinimumWidth(320);

    // The repository tabs span the whole window, sidebar included: a tab is a
    // repository, and everything below it — branches as much as history —
    // belongs to whichever one is selected.
    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(repoTabs_);
    centralLayout->addWidget(split, 1);
    setCentralWidget(central);

    // The command log: every git command, as it could be typed in a terminal.
    // A drawer rather than a pane, closed by default — most of the time the
    // commands are not what anyone is looking at, and when they are, they are
    // one shortcut away.
    commandLog_ = new QDockWidget(tr("Command Log"), this);
    commandLog_->setObjectName(QStringLiteral("commandLogDock"));
    commandLog_->setAllowedAreas(Qt::BottomDockWidgetArea);
    commandLog_->setFeatures(QDockWidget::DockWidgetClosable);
    commandLog_->setWidget(new gity::ui::CommandLogView(commandLog_));
    addDockWidget(Qt::BottomDockWidgetArea, commandLog_);
    commandLog_->hide();
}

// What the session on screen reports, through the relay (ADR-004).
void MainWindow::connectSession() {
    connect(sessions_.relay(), &gity::session::RepoSession::opened, this, &MainWindow::onOpened);
    connect(sessions_.relay(), &gity::session::RepoSession::refsReady, this,
            &MainWindow::onRefsReady);
    connect(sessions_.relay(), &gity::session::RepoSession::chunkReady, this,
            &MainWindow::onChunkReady);
    connect(sessions_.relay(), &gity::session::RepoSession::detailReady, this,
            &MainWindow::onDetailReady);
    connect(sessions_.relay(), &gity::session::RepoSession::comparisonReady, this,
            [this](gity::session::ComparisonPtr comparison) {
                if (compareTo_.isEmpty()) {
                    return; // a commit was selected while this was loading
                }
                detail_->setComparison(comparison, compareFromName_, compareToName_);
                setChangesCount(comparison ? static_cast<int>(comparison->files.size()) : -1);
                commitFiles_->setFiles(comparison ? comparison->files
                                                  : std::vector<gity::git::ChangedFile>{});
                if (comparison && !comparison->files.empty()) {
                    onFileActivated(QString::fromStdString(comparison->files.front().path));
                } else {
                    diff_->clearDiff();
                    diff_->setPlaceholder(tr("No files differ"));
                    diffStack_->setCurrentWidget(diff_);
                }
            });
    connect(sessions_.relay(), &gity::session::RepoSession::finished, this,
            &MainWindow::onFinished);
    connect(sessions_.relay(), &gity::session::RepoSession::failed, this, &MainWindow::onFailed);
    connect(sessions_.relay(), &gity::session::RepoSession::fileDiffReady, this,
            &MainWindow::onFileDiffReady);
    connect(sessions_.relay(), &gity::session::RepoSession::statusReady, this,
            &MainWindow::onStatusReady);
    connect(sessions_.relay(), &gity::session::RepoSession::imageDiffReady, this,
            &MainWindow::onImageDiffReady);
    connect(sessions_.relay(), &gity::session::RepoSession::networkStarted, this,
            &MainWindow::onNetworkStarted);
    connect(sessions_.relay(), &gity::session::RepoSession::networkProgress, this,
            &MainWindow::onNetworkProgress);
    connect(sessions_.relay(), &gity::session::RepoSession::editStopPrepared, this,
            [this](const QString& message, const QString&) {
                // The commit's changes are staged and its message is ready:
                // this is the screen where it is edited.
                changes_->restoreDraft(message, false);
                showScreen(gity::ui::RefsSidebar::Screen::WorkingCopy);
            });
    connect(sessions_.relay(), &gity::session::RepoSession::editStopChanged, operationBanner_,
            &gity::ui::OperationBanner::setEditing);
    connect(sessions_.relay(), &gity::session::RepoSession::operationChanged, this,
            [this](int operation, const QString& noun) {
                operationBanner_->setOperation(static_cast<gity::git::Operation>(operation), noun);
            });
    connect(sessions_.relay(), &gity::session::RepoSession::historyEditFinished, this,
            &MainWindow::onHistoryEditFinished);
    connect(
        sessions_.relay(), &gity::session::RepoSession::undoStateChanged, this,
        [this](const QString& undoLabel, const QString& redoLabel) {
            undoAction_->setEnabled(!undoLabel.isEmpty());
            undoAction_->setText(undoLabel.isEmpty() ? tr("&Undo") : tr("&Undo %1").arg(undoLabel));
            redoAction_->setEnabled(!redoLabel.isEmpty());
            redoAction_->setText(redoLabel.isEmpty() ? tr("&Redo") : tr("&Redo %1").arg(redoLabel));
            gity::ui::describeCommand(
                undoAction_,
                tr("Put branches, tags, stashes and the working copy back as they were "
                   "before it. What is there now is kept, and Redo brings it back."),
                QStringLiteral("update-ref · read-tree  (see the Command Log)"));
        });
    connect(sessions_.relay(), &gity::session::RepoSession::previewReady, this,
            &MainWindow::showPreview);
    connect(sessions_.relay(), &gity::session::RepoSession::headAncestorsReady, this,
            &MainWindow::offerInteractiveRebase);
    connect(sessions_.relay(), &gity::session::RepoSession::externalChange, this,
            &MainWindow::onExternalChange);
    connect(sessions_.relay(), &gity::session::RepoSession::cloneFinished, this,
            [this](const QString& path) { openRepository(path); });
    connect(sessions_.relay(), &gity::session::RepoSession::networkFinished, this,
            &MainWindow::onNetworkFinished);
    connect(sessions_.relay(), &gity::session::RepoSession::workingDiffReady, this,
            &MainWindow::onWorkingDiffReady);
    connect(sessions_.relay(), &gity::session::RepoSession::remotesReady, this,
            [this](gity::session::RemoteListPtr remotes) {
                currentRemotes_ = remotes;
                // Named for the host of origin (or the only remote): the one a
                // branch is almost always proposed on.
                QString noun;
                if (remotes && !remotes->empty()) {
                    const auto origin =
                        std::find_if(remotes->begin(), remotes->end(),
                                     [](const auto& r) { return r.name == "origin"; });
                    const auto& remote = origin != remotes->end() ? *origin : remotes->front();
                    const gity::git::Provider provider = providerFor(remote.fetchUrl);
                    if (provider != gity::git::Provider::Other &&
                        !gity::git::repositoryWebUrl(remote.fetchUrl).empty()) {
                        noun = provider == gity::git::Provider::GitLab ? tr("Merge Request")
                                                                       : tr("Pull Request");
                    }
                }
                refs_->setPullRequestNoun(noun);
            });
    connect(sessions_.relay(), &gity::session::RepoSession::locksReady, this,
            [this](gity::session::LockListPtr locks, bool supported) {
                changes_->setLocks(std::move(locks), supported);
            });
    connect(
        sessions_.relay(), &gity::session::RepoSession::commitFailed, changes_,
        [this](const QString& message, const QString&) { changes_->showCommitFailed(message); });
    connect(sessions_.relay(), &gity::session::RepoSession::committed, this,
            [this](const QString& summary) {
                changes_->showCommitted(summary);
                // A commit changes history and refs, so reopen rather than
                // patching the model in place.
                openRepository(currentWorkdir_);
            });
}

// The history: selecting a commit, comparing two branches, and reading
// one commit's files.
void MainWindow::connectHistory() {
    // Two branches selected in the sidebar: what differs between their tips —
    // the question behind most multi-selections there. Selecting a commit in the graph ends it.
    connect(refs_, &gity::ui::RefsSidebar::compareRequested, this,
            [this](const QString& fromName, const QString& fromOid, const QString& toName,
                   const QString& toOid) { startComparison(fromName, fromOid, toName, toOid); });
    connect(refs_, &gity::ui::RefsSidebar::compareTooMany, this, [this](int count) {
        endComparison();
        currentOid_.clear();
        currentRenamedPath_.clear();
        graph_->clearSelection();
        clearCommitView();
        detail_->setCannotCompare(count);
        setSummary(tr("%1 branches selected").arg(count),
                   tr("Select exactly two to compare them."));
    });
    // Clicking a branch or tag takes the graph to its tip. Without this the
    // sidebar navigated to the history screen and left it wherever it was,
    // which is not what clicking a branch means.
    connect(refs_, &gity::ui::RefsSidebar::refActivated, this,
            [this](const QString&, const QString& oid) {
                const qsizetype row = model_.rowForId(oid);
                if (row >= 0) {
                    graph_->selectRow(row);
                }
            });
    connect(detail_, &gity::ui::CommitDetailPanel::swapRequested, this, [this] {
        // Copies: startComparison assigns these very members, and passing them
        // by reference compared the second branch with itself.
        const QString fromName = compareToName_;
        const QString fromOid = compareTo_;
        const QString toName = compareFromName_;
        const QString toOid = compareFrom_;
        startComparison(fromName, fromOid, toName, toOid);
    });
    connect(graph_, &gity::ui::CommitGraphView::rowSelected, this, &MainWindow::onRowSelected);
    // A file clicked on the Commit tab opens the Changes tab on it; a file
    // picked in the Changes tab's own list shows its diff there. A lambda
    // rather than the slot directly: a default argument does not satisfy Qt's
    // signal/slot arity check, and this call site is the user-initiated one.
    connect(detail_, &gity::ui::CommitDetailPanel::fileActivated, this,
            [this](const QString& path) {
                commitTabs_->setCurrentIndex(kChangesTab);
                commitFiles_->selectPath(path);
            });
    connect(commitFiles_, &gity::ui::ChangedFilesList::fileSelected, this,
            &MainWindow::onFileActivated);
    connect(graph_, &gity::ui::CommitGraphView::rowContextMenuRequested, this,
            &MainWindow::onCommitContextMenu);
}

// What is asked for from the sidebar's branches, tags, stashes and
// submodules.
void MainWindow::connectSidebar() {
    connect(refs_, &gity::ui::RefsSidebar::screenRequested, this, &MainWindow::showScreen);
    connect(refs_, &gity::ui::RefsSidebar::checkoutRequested, this, &MainWindow::confirmCheckout);
    connect(refs_, &gity::ui::RefsSidebar::submoduleActivated, this, &MainWindow::openSubmodule);
    connect(refs_, &gity::ui::RefsSidebar::pullRequested, this,
            [this] { askPreview(gity::session::Preview::Kind::Pull); });
    connect(refs_, &gity::ui::RefsSidebar::pushTagRequested, this, [this](auto&&... args) {
        session()->requestPushTag(std::forward<decltype(args)>(args)...);
    });
    connect(refs_, &gity::ui::RefsSidebar::checkoutRefRequested, this,
            &MainWindow::confirmCheckout);
    connect(refs_, &gity::ui::RefsSidebar::deleteTagRequested, this, [this](const QString& name) {
        // The button says what it does, and Cancel is the default.
        if (gity::ui::confirmDestructive(
                this, tr("Delete tag?"), tr("Delete \"%1\" locally?").arg(name),
                tr("A tag already pushed stays on the remote until it is deleted "
                   "there too. Edit ▸ Undo brings the local one back."),
                tr("Delete Tag"))) {
            session()->requestDeleteTag(name);
        }
    });
    connect(refs_, &gity::ui::RefsSidebar::pushRefRequested, this, [this](const QString& branch) {
        askPreview(gity::session::Preview::Kind::Push, {}, branch);
    });
    connect(refs_, &gity::ui::RefsSidebar::mergeRequested, this,
            [this](const QString& ref) { askPreview(gity::session::Preview::Kind::Merge, ref); });
    connect(refs_, &gity::ui::RefsSidebar::newBranchFromRequested, this,
            &MainWindow::openNewBranchDialog);
    connect(refs_, &gity::ui::RefsSidebar::rebaseOntoRequested, this,
            [this](const QString& ref) { askPreview(gity::session::Preview::Kind::Rebase, ref); });
    connect(refs_, &gity::ui::RefsSidebar::interactiveRebaseRequested, this,
            [this](const QString& oid, const QString& label) { startRebase(oid, label); });
    connect(refs_, &gity::ui::RefsSidebar::stashApplyRequested, this, [this](auto&&... args) {
        session()->requestStashApply(std::forward<decltype(args)>(args)...);
    });
    connect(refs_, &gity::ui::RefsSidebar::stashDropRequested, this, [this](const QString& ref) {
        if (gity::ui::confirmDestructive(
                this, tr("Drop stash?"), tr("Discard %1?").arg(ref),
                tr("Its changes leave the stash list. Edit ▸ Undo puts it back, for "
                   "as long as git keeps unreachable objects (30 days by default)."),
                tr("Drop Stash"))) {
            session()->requestStashDrop(ref);
        }
    });
    connect(refs_, &gity::ui::RefsSidebar::pinRequested, this,
            [this](const QString& fullName, bool pin) {
                if (currentWorkdir_.isEmpty()) {
                    return;
                }
                QStringList pinned = saved::pinnedBranches(currentWorkdir_);
                pinned.removeAll(fullName);
                if (pin) {
                    pinned << fullName;
                }
                // Per repository, by path, in Gity's own settings: a pin is a
                // way of looking at the repository, not something to write
                // into it.
                saved::setPinnedBranches(currentWorkdir_, pinned);
                refs_->setPinned(pinned);
            });
    connect(
        refs_, &gity::ui::RefsSidebar::deleteBranchRequested, this,
        [this](const QString& name, bool) {
            const auto answer = QMessageBox::question(
                this, tr("Delete branch?"),
                tr("Delete \"%1\"?\n\nCommits it shares with another branch are kept.").arg(name),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer == QMessageBox::Yes) {
                pendingBranchDelete_ = name;
                session()->requestDeleteBranch(name, false);
            }
        });
    connect(refs_, &gity::ui::RefsSidebar::pullRequestRequested, this,
            &MainWindow::openPullRequestPage);
}

// Local Changes, and the banner of an operation in progress.
void MainWindow::connectWorkingCopy() {
    // The commit box's text goes along: at an edit stop, Continue commits the
    // staged edit with it.
    connect(operationBanner_, &gity::ui::OperationBanner::continueRequested, this,
            [this] { session()->requestContinueOperation(changes_->draftMessage()); });
    connect(operationBanner_, &gity::ui::OperationBanner::abortRequested, this, [this] {
        if (gity::ui::confirmDestructive(
                this, tr("Abandon?"),
                tr("Return the working copy to where it was before this operation started?"),
                tr("Anything you have resolved since then is discarded. Commits already in "
                   "history are untouched."),
                tr("Abandon"))) {
            session()->requestAbortOperation();
        }
    });
    connect(changes_, &gity::ui::WorkingCopyPanel::discardRequested, this, [this](auto&&... args) {
        session()->requestDiscardPaths(std::forward<decltype(args)>(args)...);
    });
    connect(changes_, &gity::ui::WorkingCopyPanel::stashRequested, this,
            [this](const QStringList& paths) {
                session()->requestStashSave(
                    tr("Gity: %n file(s)", nullptr, static_cast<int>(paths.size())), true, paths);
            });
    connect(changes_, &gity::ui::WorkingCopyPanel::lockRequested, this, [this](auto&&... args) {
        session()->requestLock(std::forward<decltype(args)>(args)...);
    });
    connect(changes_, &gity::ui::WorkingCopyPanel::unlockRequested, this, [this](auto&&... args) {
        session()->requestUnlock(std::forward<decltype(args)>(args)...);
    });
    connect(changes_, &gity::ui::WorkingCopyPanel::stageRequested, this, [this](auto&&... args) {
        session()->requestStage(std::forward<decltype(args)>(args)...);
    });
    connect(changes_, &gity::ui::WorkingCopyPanel::unstageRequested, this, [this](auto&&... args) {
        session()->requestUnstage(std::forward<decltype(args)>(args)...);
    });
    connect(changes_, &gity::ui::WorkingCopyPanel::diffRequested, this, [this](auto&&... args) {
        session()->requestWorkingDiff(std::forward<decltype(args)>(args)...);
    });
    connect(changes_, &gity::ui::WorkingCopyPanel::commitRequested, this, [this](auto&&... args) {
        session()->requestCommit(std::forward<decltype(args)>(args)...);
    });
    connect(changes_, &gity::ui::WorkingCopyPanel::applyPatchRequested, this,
            [this](auto&&... args) {
                session()->requestApplyPatch(std::forward<decltype(args)>(args)...);
            });
    connect(changes_, &gity::ui::WorkingCopyPanel::discardHunkRequested, this,
            [this](auto&&... args) {
                session()->requestDiscardPatch(std::forward<decltype(args)>(args)...);
            });
}

// The repository tabs, the sessions behind them, and coming back to the
// window.
void MainWindow::connectRepositoryTabs() {
    // Where the working tree is not watched (macOS and Windows for now), or
    // a watch limit was reached, coming back to the window is when an edit
    // made elsewhere is most likely to be looked for.
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                if (state == Qt::ApplicationActive && !currentWorkdir_.isEmpty() &&
                    !session()->busy()) {
                    session()->requestStatus();
                }
            });
    connect(repoTabs_, &gity::ui::RepositoryTabs::tabsChanged, this, [this] {
        if (!persistSession_) {
            return;
        }
        saved::Tabs open;
        QStringList paths;
        for (const QVariant& tab : repoTabs_->state()) {
            const QString path = tab.toMap().value(QStringLiteral("path")).toString();
            paths << path;
            open.tabs.append({path, tab.toMap().value(QStringLiteral("parent")).toString()});
        }
        for (const saved::Tab& missing : std::as_const(unavailableTabs_)) {
            if (!paths.contains(missing.path)) {
                open.tabs.append(missing);
            }
        }
        open.current = repoTabs_->currentIndex();
        saved::saveOpenTabs(open);
    });
    connect(repoTabs_, &gity::ui::RepositoryTabs::repositoryClosed, this,
            [this](const QString& path) { retireSession(path); });
    connect(&sessions_, &gity::session::SessionHub::backgroundNetworkFinished, this,
            &MainWindow::reportBackgroundNetwork);
    connect(&sessions_, &gity::session::SessionHub::backgroundCloneFinished, this,
            [this](const QString& cloned) { openRepository(cloned); });
    connect(repoTabs_, &gity::ui::RepositoryTabs::repositoryActivated, this,
            [this](const QString& path) {
                // Compared canonically, not as raw strings: the same
                // directory reaches this code spelled more than one way, and
                // assuming otherwise is what emptied the tab set.
                if (path.isEmpty() || gity::ui::RepositoryTabs::canonical(path) ==
                                          gity::ui::RepositoryTabs::canonical(currentWorkdir_)) {
                    return;
                }
                switchingRepository_ = true;
                openRepository(path);
                switchingRepository_ = false;
            });
}

void MainWindow::showChangesTab() {
    if (tabs_ != nullptr) {
        tabs_->setCurrentIndex(ScreenChanges);
    }
}

void MainWindow::showImageTab(const QString& path) {
    // The working copy's version against HEAD, in the Changes tab's diff area.
    // A development aid (--image); nothing in the window routes here.
    tabs_->setCurrentIndex(ScreenHistory);
    commitTabs_->setCurrentIndex(kChangesTab);
    diffStack_->setCurrentWidget(image_);
    if (!path.isEmpty()) {
        session()->requestImageDiff(path);
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    // The history header, left showing when the graph is dragged down to it,
    // brings the graph back when clicked.
    if (event->type() == QEvent::MouseButtonPress && historyHeaderRestore_ &&
        watched == filterBar_) {
        historyHeaderRestore_();
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::clearCommitView() {
    endComparison();
    // Everything under the graph, including the image comparison: switching
    // to another repository left the last one's image on screen.
    detail_->clearDetail();
    commitFiles_->clear();
    diff_->clearDiff();
    diff_->setPlaceholder(tr("Select a file to see its diff"));
    image_->clearDiff();
    diffStack_->setCurrentWidget(diff_);
    setSummary(tr("No commit selected"), {});
    setChangesCount(-1);
}

void MainWindow::setSummary(const QString& title, const QString& meta) {
    summaryTitle_->setText(title);
    summaryMeta_->setText(meta);
    summaryMeta_->setVisible(!meta.isEmpty());
}

void MainWindow::setChangesCount(int files) {
    commitTabs_->setTabText(kChangesTab,
                            files < 0 ? tr("Changes") : tr("Changes · %L1").arg(files));
}

void MainWindow::onDetailReady(gity::session::DetailPtr detail) {
    detail_->setDetail(detail);
    commitFiles_->setDetail(detail);
    setChangesCount(detail ? static_cast<int>(detail->files.size()) : -1);

    // Land on the first changed file so the diff pane has content without a
    // second click — or, while the history is of one file or folder, on the
    // file it is the history of.
    if (detail && !detail->files.empty()) {
        QString landing = QString::fromStdString(detail->files.front().path);
        const QString wanted = !currentRenamedPath_.isEmpty()
                                   ? currentRenamedPath_
                                   : QString::fromStdString(walkedFilter_.path);
        if (!wanted.isEmpty()) {
            for (const auto& file : detail->files) {
                const QString path = QString::fromStdString(file.path);
                if (path == wanted || path.startsWith(wanted + QChar('/'))) {
                    landing = path;
                    break;
                }
            }
            const QSignalBlocker quiet(commitFiles_);
            commitFiles_->selectPath(landing);
        }
        onFileActivated(landing);
    } else {
        diff_->clearDiff();
        diff_->setPlaceholder(detail ? tr("This commit changes no files")
                                     : tr("Select a file to see its diff"));
        diffStack_->setCurrentWidget(diff_);
    }
}

void MainWindow::onFileActivated(const QString& path) {
    const bool comparing = !compareTo_.isEmpty();
    if (path.isEmpty() || (!comparing && currentOid_.isEmpty())) {
        return;
    }

    // An image has no useful textual diff: its before and after are shown in
    // the diff's place — the commit against its parent, or one branch tip
    // against the other.
    static const QStringList imageSuffixes{"png", "jpg", "jpeg", "tga",  "psd",
                                           "bmp", "gif", "tif",  "tiff", "webp"};
    if (imageSuffixes.contains(QFileInfo(path).suffix().toLower())) {
        diffStack_->setCurrentWidget(image_);
        if (comparing) {
            session()->requestImageDiff(path, compareTo_, compareFrom_);
        } else {
            session()->requestImageDiff(path, currentOid_);
        }
        return;
    }

    diffStack_->setCurrentWidget(diff_);
    diff_->setPlaceholder(tr("Loading %1…").arg(path));
    diff_->clearDiff();
    if (comparing) {
        session()->requestFileDiffBetween(compareFrom_, compareTo_, path);
    } else {
        session()->requestFileDiff(currentOid_, path);
    }
}

void MainWindow::startComparison(const QString& fromName, const QString& fromOid,
                                 const QString& toName, const QString& toOid) {
    compareFromName_ = fromName;
    compareFrom_ = fromOid;
    compareToName_ = toName;
    compareTo_ = toOid;

    // Not one commit, so no row is selected: a highlighted row would claim the
    // tabs below describe it.
    currentOid_.clear();
    currentRenamedPath_.clear();
    graph_->clearSelection();
    commitFiles_->clear();
    diff_->clearDiff();
    image_->clearDiff();
    diffStack_->setCurrentWidget(diff_);
    diff_->setPlaceholder(tr("Comparing…"));
    detail_->setPendingComparison(fromName, toName);
    setSummary(tr("Comparing %1 → %2").arg(fromName, toName),
               tr("tip to tip — the files as they are at each branch's latest commit"));
    setChangesCount(-1);
    session()->requestComparison(fromOid, toOid);
}

void MainWindow::endComparison() {
    compareFrom_.clear();
    compareTo_.clear();
    compareFromName_.clear();
    compareToName_.clear();
}

void MainWindow::onFileDiffReady(gity::session::FileDiffPtr diff) {
    diff_->setDiff(std::move(diff));
}

void MainWindow::showScreen(gity::ui::RefsSidebar::Screen screen) {
    switch (screen) {
    case gity::ui::RefsSidebar::Screen::WorkingCopy:
        tabs_->setCurrentIndex(ScreenChanges);
        break;
    case gity::ui::RefsSidebar::Screen::History:
        tabs_->setCurrentIndex(ScreenHistory);
        break;
    }
    // Keep the sidebar showing where you are, however you got there — the
    // menu, a shortcut, or the row itself.
    refs_->selectScreen(screen);
}

void MainWindow::onImageDiffReady(gity::session::ImageDiffPtr diff) {
    image_->setDiff(std::move(diff));
}

void MainWindow::onWorkingDiffReady(gity::session::FileDiffPtr diff,
                                    gity::session::HunkStagingPtr staging) {
    changes_->setDiff(std::move(diff), std::move(staging));
}

void MainWindow::onRowSelected(qsizetype row) {
    if (row < 0) {
        return;
    }
    const auto index = static_cast<std::size_t>(row);
    endComparison();

    // Show what we already have immediately; the file list follows once the
    // diff comes back, so the panel never appears to describe the old commit.
    currentOid_ = model_.fullId(index);
    currentRenamedPath_ = model_.renamedPath(index);
    detail_->setPending(model_.shortId(index), model_.summary(index));
    setSummary(model_.summary(index),
               QStringLiteral("%1  ·  %2  ·  %3")
                   .arg(model_.author(index), model_.when(index), model_.shortId(index)));
    setChangesCount(-1);
    session()->requestDetail(currentOid_);
}

} // namespace gity::app
