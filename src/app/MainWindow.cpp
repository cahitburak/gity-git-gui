#include "MainWindow.h"

#include "SavedState.h"

#include "core/git/Provider.h"
#include "core/git/PullRequest.h"
#include "session/Accounts.h"
#include "session/GitProcess.h"
#include "session/RepoWatcher.h"
#include "ui/diffview/DiffView.h"
#include "ui/graphview/CommitGraphView.h"
#include "ui/graphview/GraphHeader.h"
#include "ui/graphview/HistoryFilterBar.h"
#include "ui/image/ImageDiffView.h"
#include "ui/panels/CloneDialog.h"
#include "ui/panels/CommandLogView.h"
#include "ui/panels/CommitDetailPanel.h"
#include "ui/panels/Confirm.h"
#include "ui/panels/OperationBanner.h"
#include "ui/panels/PreviewPrompt.h"
#include "ui/panels/RebaseDialog.h"
#include "ui/panels/RefsSidebar.h"
#include "ui/panels/RemotesDialog.h"
#include "ui/panels/RepositoryTabs.h"
#include "ui/panels/SettingsDialog.h"
#include "ui/panels/SwitchDialogs.h"
#include "ui/panels/WelcomePanel.h"
#include "ui/panels/WorkingCopyPanel.h"
#include "ui/theme/Theme.h"
#include "ui/theme/Tokens.h"
#include "ui/theme/ToolbarIcons.h"

#include <QCloseEvent>
#include <QDir>

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <git2.h>

namespace gity::app {
namespace tokens = gity::ui::tokens;

namespace {
/// Screens, in the order they are added to the stack. Named because the
/// indices were literals, and deleting a screen quietly repointed every
/// literal after it at the wrong one.
enum Screen : int { ScreenHistory = 0, ScreenChanges = 1, ScreenWelcome = 2 };

/// The Changes tab, under the graph.
constexpr int kChangesTab = 1;
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
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

    buildMenus();
    buildToolBar();
    buildStatusBar();
    syncNetworkState(); // nothing is open yet

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
    // Two branches selected in the sidebar: what differs between their tips —
    // the question behind most multi-selections there. Selecting a commit in the graph ends it.
    connect(refs_, &gity::ui::RefsSidebar::compareRequested, this,
            [this](const QString& fromName, const QString& fromOid, const QString& toName,
                   const QString& toOid) { startComparison(fromName, fromOid, toName, toOid); });
    connect(refs_, &gity::ui::RefsSidebar::compareTooMany, this, [this](int count) {
        endComparison();
        currentOid_.clear();
        graph_->clearSelection();
        clearCommitView();
        detail_->setCannotCompare(count);
        setSummary(tr("%1 branches selected").arg(count),
                   tr("Select exactly two to compare them."));
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
    connect(sessions_.relay(), &gity::session::RepoSession::finished, this,
            &MainWindow::onFinished);
    connect(sessions_.relay(), &gity::session::RepoSession::failed, this, &MainWindow::onFailed);
    connect(sessions_.relay(), &gity::session::RepoSession::fileDiffReady, this,
            &MainWindow::onFileDiffReady);
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
    connect(refs_, &gity::ui::RefsSidebar::screenRequested, this, &MainWindow::showScreen);

    connect(sessions_.relay(), &gity::session::RepoSession::statusReady, this,
            &MainWindow::onStatusReady);
    connect(sessions_.relay(), &gity::session::RepoSession::imageDiffReady, this,
            &MainWindow::onImageDiffReady);
    connect(sessions_.relay(), &gity::session::RepoSession::networkStarted, this,
            &MainWindow::onNetworkStarted);
    connect(sessions_.relay(), &gity::session::RepoSession::networkProgress, this,
            &MainWindow::onNetworkProgress);
    // The commit box's text goes along: at an edit stop, Continue commits the
    // staged edit with it.
    connect(operationBanner_, &gity::ui::OperationBanner::continueRequested, this,
            [this] { session()->requestContinueOperation(changes_->draftMessage()); });
    connect(sessions_.relay(), &gity::session::RepoSession::editStopPrepared, this,
            [this](const QString& message, const QString&) {
                // The commit's changes are staged and its message is ready:
                // this is the screen where it is edited.
                changes_->restoreDraft(message, false);
                showScreen(gity::ui::RefsSidebar::Screen::WorkingCopy);
            });
    connect(sessions_.relay(), &gity::session::RepoSession::editStopChanged, operationBanner_,
            &gity::ui::OperationBanner::setEditing);
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
    connect(graph_, &gity::ui::CommitGraphView::rowContextMenuRequested, this,
            &MainWindow::onCommitContextMenu);
    connect(refs_, &gity::ui::RefsSidebar::checkoutRequested, this, &MainWindow::confirmCheckout);
    connect(refs_, &gity::ui::RefsSidebar::submoduleActivated, this, &MainWindow::openSubmodule);
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
    connect(sessions_.relay(), &gity::session::RepoSession::previewReady, this,
            &MainWindow::showPreview);
    connect(sessions_.relay(), &gity::session::RepoSession::headAncestorsReady, this,
            &MainWindow::offerInteractiveRebase);
    connect(sessions_.relay(), &gity::session::RepoSession::externalChange, this,
            &MainWindow::onExternalChange);
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
    connect(refs_, &gity::ui::RefsSidebar::interactiveRebaseRequested, this,
            [this](const QString& oid, const QString& label) { startRebase(oid, label); });
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
    connect(sessions_.relay(), &gity::session::RepoSession::cloneFinished, this,
            [this](const QString& path) { openRepository(path); });
    connect(sessions_.relay(), &gity::session::RepoSession::networkFinished, this,
            &MainWindow::onNetworkFinished);
    connect(sessions_.relay(), &gity::session::RepoSession::workingDiffReady, this,
            &MainWindow::onWorkingDiffReady);
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
    connect(refs_, &gity::ui::RefsSidebar::pullRequestRequested, this,
            &MainWindow::openPullRequestPage);
    connect(sessions_.relay(), &gity::session::RepoSession::locksReady, this,
            [this](gity::session::LockListPtr locks, bool supported) {
                changes_->setLocks(std::move(locks), supported);
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

void MainWindow::rememberRepository(const QString& path) {
    if (!persistSession_) {
        return;
    }
    welcome_->setRecent(saved::rememberRepository(path));
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

void MainWindow::setSessionPersistence(bool enabled) {
    persistSession_ = enabled;
    if (!enabled) {
        return;
    }
    // Pane sizes come back as the user left them, and are saved as they are
    // dragged. Every named splitter takes part — the ones inside panels too —
    // so a new one only needs a name (2026-09 UI review).
    for (QSplitter* splitter : findChildren<QSplitter*>()) {
        const QString name = splitter->objectName();
        if (name.isEmpty()) {
            continue;
        }
        const QByteArray state = saved::splitterState(name);
        if (!state.isEmpty()) {
            // A saved state carries its own collapsible flag — and sizes from
            // when panes could still be collapsed to nothing — so the rule is
            // put back afterwards, and the sizes clamped to the floors with it.
            splitter->restoreState(state);
            splitter->setChildrenCollapsible(false);
            splitter->setSizes(splitter->sizes());
        }
        connect(splitter, &QSplitter::splitterMoved, this, [this, splitter, name] {
            if (persistSession_) {
                saved::saveSplitterState(name, splitter->saveState());
            }
        });
    }
}

void MainWindow::restoreSession(bool openCurrent) {
    const saved::Tabs tabs = saved::openTabs();

    // Repositories that cannot be reached are left out rather than reopened
    // into an error dialog each — but not forgotten: one on a drive that is
    // not mounted today is kept in the saved list, so it comes back when the
    // drive does. Only a folder that is there with no repository in it is
    // dropped for good.
    QString currentPath;
    unavailableTabs_.clear();
    for (int i = 0; i < tabs.tabs.size(); ++i) {
        const saved::Tab& tab = tabs.tabs.at(i);
        if (tab.path.isEmpty()) {
            continue;
        }
        if (!QFileInfo::exists(tab.path + QStringLiteral("/.git"))) {
            if (!QFileInfo::exists(tab.path)) {
                unavailableTabs_ << tab;
            }
            continue;
        }
        repoTabs_->open(tab.path, tab.parent);
        if (i == tabs.current || currentPath.isEmpty()) {
            currentPath = tab.path;
        }
    }
    if (openCurrent && !currentPath.isEmpty()) {
        openRepository(currentPath);
    }
}

void MainWindow::chooseRepository() {
    const QString path = QFileDialog::getExistingDirectory(this, tr("Open Repository"));
    if (!path.isEmpty()) {
        openRepository(path);
    }
}

void MainWindow::openRepository(const QString& path) {
    if (path.isEmpty()) {
        return;
    }
    // Whether this is a refresh of what is already open — after a fetch, a
    // commit, a checkout — rather than a different repository. A refresh keeps
    // what the user is in the middle of typing.
    const bool refreshing =
        !currentWorkdir_.isEmpty() && gity::ui::RepositoryTabs::canonical(path) ==
                                          gity::ui::RepositoryTabs::canonical(currentWorkdir_);
    if (tabs_->currentIndex() == ScreenWelcome) {
        tabs_->setCurrentIndex(ScreenHistory);
        refs_->selectScreen(gity::ui::RefsSidebar::Screen::History);
    }

    // A repository that already has a tab is switched to; anything else gets
    // a tab of its own, beside the ones already open. Reopening
    // the same path after a fetch, a merge or a branch switch finds its own
    // tab and changes nothing.
    if (!repoTabs_->activate(path)) {
        repoTabs_->open(path);
    }

    // What each repository was showing is kept while another is on screen,
    // and put back on return: the selected commit, the filter, a half-written
    // message, which screen and which lower tab. A refresh of the same one
    // keeps its selected commit instead of jumping back to the newest
    // (2026-09 UI review).
    ViewState arriving;
    if (refreshing) {
        pendingSelectOid_ = currentOid_;
    } else {
        if (!currentWorkdir_.isEmpty()) {
            ViewState leaving;
            leaving.selectedOid = currentOid_;
            leaving.filter = filterBar_->filter();
            leaving.draft = changes_->draftMessage();
            leaving.amend = changes_->draftAmend();
            leaving.screen = tabs_->currentIndex() == ScreenChanges ? ScreenChanges : ScreenHistory;
            leaving.lowerTab = commitTabs_->currentIndex();
            viewStates_.insert(gity::ui::RepositoryTabs::canonical(currentWorkdir_), leaving);
        }
        arriving = viewStates_.value(gity::ui::RepositoryTabs::canonical(path));
        pendingSelectOid_ = arriving.selectedOid;
        filterBar_->setFilter(arriving.filter);
        showFilterState(arriving.filter);
        // A different repository has different contributors; the list is
        // rebuilt when its history arrives.
        filterBar_->setContributors({});
    }

    if (!refreshing) {
        activateSession(path);
    }

    // Counters describe the repository being left; a stale one would follow
    // you into a submodule and describe the parent as the child.
    changedFiles_ = 0;

    model_.clear();
    graph_->setModel(&model_);
    refs_->clearRefs();
    clearCommitView();
    changes_->clearAll(refreshing);
    currentOid_.clear();
    firstChunkSeen_ = false;
    sinceOpen_.restart();

    repoLabel_->setText(tr("Opening %1…").arg(path));
    countLabel_->clear();

    if (refreshing) {
        session()->open(path);
        return;
    }
    changes_->restoreDraft(arriving.draft, arriving.amend);
    commitTabs_->setCurrentIndex(arriving.lowerTab);
    showScreen(arriving.screen == ScreenChanges ? gity::ui::RefsSidebar::Screen::WorkingCopy
                                                : gity::ui::RefsSidebar::Screen::History);
    walkedFilter_ = arriving.filter;
    session()->open(path, arriving.filter);
}

gity::git::Provider MainWindow::providerFor(const std::string& remoteUrl) {
    const gity::git::Provider guessed =
        gity::git::providerForHost(gity::git::hostOfRemote(remoteUrl));
    if (guessed != gity::git::Provider::Other) {
        return guessed;
    }
    // A self-hosted server with an unhelpful name — git.studio.io — is still
    // known if an account was added for it, with its provider chosen then.
    for (const auto& account :
         gity::session::Accounts::forRemote(QString::fromStdString(remoteUrl))) {
        if (account.provider != gity::git::Provider::Other) {
            return account.provider;
        }
    }
    return gity::git::Provider::Other;
}

void MainWindow::openPullRequestPage(const QString& remoteBranch) {
    if (!currentRemotes_) {
        return;
    }
    // The remote is the longest remote name the branch starts with, so a
    // remote called "team/a" is not mistaken for "team".
    const gity::git::RemoteEntry* remote = nullptr;
    for (const auto& candidate : *currentRemotes_) {
        const QString prefix = QString::fromStdString(candidate.name) + QChar('/');
        if (remoteBranch.startsWith(prefix) &&
            (remote == nullptr || candidate.name.size() > remote->name.size())) {
            remote = &candidate;
        }
    }
    if (remote == nullptr) {
        return;
    }
    const QString remoteName = QString::fromStdString(remote->name);
    const QString source = remoteBranch.mid(remoteName.size() + 1);
    // Into the remote's default branch, as its HEAD names it — the release
    // branch, in a project that makes each release the default.
    QString target;
    if (currentRefs_) {
        for (const auto& name : currentRefs_->remoteDefaults) {
            const QString full = QString::fromStdString(name);
            if (full.startsWith(remoteName + QChar('/'))) {
                target = full.mid(remoteName.size() + 1);
            }
        }
    }
    if (target == source) {
        target.clear(); // the host will ask
    }
    const auto url = gity::git::newPullRequestUrl(providerFor(remote->fetchUrl), remote->fetchUrl,
                                                  source.toStdString(), target.toStdString());
    if (!url) {
        statusBar()->showMessage(
            tr("%1's host is not one Gity knows how to open a page on.").arg(remoteName), 6000);
        return;
    }
    QDesktopServices::openUrl(QUrl(QString::fromStdString(*url), QUrl::StrictMode));
    statusBar()->showMessage(tr("Opened the new %1 page for %2 in your browser.")
                                 .arg(QString::fromStdString(std::string(gity::git::pullRequestNoun(
                                          providerFor(remote->fetchUrl)))),
                                      source),
                             6000);
}

void MainWindow::showFilterState(const gity::git::CommitFilter& filter) {
    filterBar_->showState(filter.active());
    graph_->setPlaceholder(filter.active() ? tr("No commit matches these filters") : QString());
}

void MainWindow::applyCommitFilter() {
    if (currentWorkdir_.isEmpty()) {
        return;
    }
    const gity::git::CommitFilter filter = filterBar_->filter();
    if (filter == walkedFilter_) {
        return;
    }
    walkedFilter_ = filter;
    // The model has to be emptied before the new walk streams into it, or
    // the filtered rows are appended to the unfiltered ones and the view
    // shows both sets at once.
    model_.clear();
    graph_->setModel(&model_);
    clearCommitView();
    currentOid_.clear();
    showFilterState(filter);
    session()->requestFilter(filter);
}

void MainWindow::refreshContributors() {
    // From the history just walked, and only an unfiltered one: a list built
    // from matches would offer only the author already chosen. The newest
    // 200,000 commits are plenty to name everyone active.
    QHash<QString, int> counts;
    const std::size_t rows = std::min<std::size_t>(model_.rowCount(), 200000);
    for (std::size_t row = 0; row < rows; ++row) {
        ++counts[model_.author(row)];
    }
    QList<QPair<QString, int>> ordered;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        ordered.append({it.key(), it.value()});
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    filterBar_->setContributors(ordered);
}

void MainWindow::onOpened(const gity::session::RepoInfo& info) {
    repoLabel_->setText(QStringLiteral("%1  ·  %2").arg(info.workdir, info.headRef));
    currentWorkdir_ = info.workdir;
    sessions_.markOpened();
    syncNetworkState();
    updateRepositoryTitle();
    refs_->setRepositoryName(QDir(info.workdir).dirName());
    refs_->setPinned(saved::pinnedBranches(info.workdir));
    changes_->setWorkdir(info.workdir);
    // Remembered once it has actually opened — a path that failed is not
    // worth offering again — and only if opened in its own right: a
    // submodule is reached through its parent.
    if (repoTabs_->currentParentName().isEmpty()) {
        rememberRepository(info.workdir);
    }

    // Locks are a network round trip, so they are asked for once on open and
    // then only after a lock or unlock. Nothing waits on the answer.
    session()->requestLocks();
    // Which account this repository uses, for the status bar and Settings.
    session()->requestAccountChoice();
    // The Accounts section needs origin's URL, and asking on open means it is
    // there whenever settings are opened rather than only after the Remotes
    // dialog has been visited.
    session()->requestRemotes();
}

void MainWindow::onRefsReady(gity::session::RefSetPtr refs) {
    refs_->setRefs(refs);
    // The graph paints ref chips from the same RefSet the sidebar lists, so
    // the two can never disagree about where a branch points.
    model_.setRefs(refs);
    currentRefs_ = refs;
    if (refs) {
        syncNetworkState();
        // Merge needs somewhere to merge from.
        const bool somewhereToGo = refs->localBranches.size() + refs->remoteBranches.size() > 1;
        mergeAction_->setEnabled(somewhereToGo);
        rebaseAction_->setEnabled(somewhereToGo);
        branchAction_->setEnabled(true);
        stashAction_->setEnabled(true);
        // SPEC.md puts the counts on the buttons. They come from the checked-out
        // branch's upstream, which is the only one Pull and Push act on.
        for (const auto& branch : refs->localBranches) {
            if (!branch.isHead) {
                continue;
            }
            pullAction_->setText(branch.behind > 0 ? tr("Pull  %1").arg(branch.behind)
                                                   : tr("Pull"));
            pushAction_->setText(branch.ahead > 0 ? tr("Push  %1").arg(branch.ahead) : tr("Push"));
            gity::ui::describeCommand(
                pushAction_,
                branch.hasUpstream ? tr("Send %1's new commits to %2.")
                                         .arg(QString::fromStdString(branch.name),
                                              QString::fromStdString(branch.upstream))
                                   : tr("Publish %1 — it tracks no upstream branch yet.")
                                         .arg(QString::fromStdString(branch.name)),
                branch.hasUpstream ? QStringLiteral("push")
                                   : QStringLiteral("push --set-upstream origin %1")
                                         .arg(QString::fromStdString(branch.name)));
            break;
        }
    }
    graph_->rowsAppended();
    if (refs) {
        statusBar()->showMessage(tr("%L1 branches, %L2 remotes, %L3 tags, %L4 stashes")
                                     .arg(refs->localBranches.size())
                                     .arg(refs->remoteBranches.size())
                                     .arg(refs->tags.size())
                                     .arg(refs->stashes.size()),
                                 4000);
    }
}

void MainWindow::onChunkReady(gity::session::ChunkPtr chunk) {
    const std::size_t firstNewRow = model_.rowCount();
    model_.append(std::move(chunk));
    graph_->rowsAppended();

    // The commit that was selected before, found as the history streams in —
    // only the chunk just added is searched.
    if (!pendingSelectOid_.isEmpty()) {
        const qsizetype row = model_.rowForId(pendingSelectOid_, firstNewRow);
        if (row >= 0) {
            pendingSelectOid_.clear();
            firstChunkSeen_ = true;
            graph_->selectRow(row);
        }
    }

    if (!firstChunkSeen_ && pendingSelectOid_.isEmpty()) {
        firstChunkSeen_ = true;
        // Land on the newest commit so the detail panel has something to show
        // rather than opening blank.
        graph_->selectRow(0);
    }
    countLabel_->setText(tr("%L1 commits…").arg(model_.rowCount()));
    graphHeader_->setMaxLanes(model_.maxLanes());
}

void MainWindow::onFinished(quint64 commits, double firstChunkMs, double totalMs, bool cancelled) {
    // The remembered commit is gone — reset away, or filtered out: land on
    // the newest, as a fresh open would.
    if (!pendingSelectOid_.isEmpty() && !cancelled) {
        pendingSelectOid_.clear();
        if (currentOid_.isEmpty() && model_.rowCount() > 0) {
            graph_->selectRow(0);
        }
    }
    if (cancelled) {
        countLabel_->setText(tr("cancelled at %L1").arg(model_.rowCount()));
        return;
    }
    Q_UNUSED(firstChunkMs)
    Q_UNUSED(totalMs)
    countLabel_->setText(tr("%Ln commit(s)", nullptr, static_cast<int>(commits)));
    if (!walkedFilter_.active()) {
        refreshContributors();
    }
}

void MainWindow::onFailed(const QString& message, const QString& detail) {
    // A repository that never opened leaves no tab and no session behind:
    // otherwise its tab stays, and the toolbar acts on whatever was open
    // before — a repository no longer on screen.
    if (sessions_.currentNeverOpened()) {
        const QString failedPath = sessions_.currentPath();
        currentWorkdir_.clear();
        // After this signal has returned: the session sending it is about to
        // be destroyed, and must not be while it is still emitting.
        QTimer::singleShot(0, this, [this, failedPath] {
            repoTabs_->close(failedPath); // may bring another tab forward
            retireSession(failedPath);
            if (repoTabs_->count() == 0) {
                tabs_->setCurrentIndex(ScreenWelcome);
            }
        });
    }
    repoLabel_->setText(currentWorkdir_.isEmpty() ? tr("No repository open") : repoLabel_->text());
    QMessageBox box(QMessageBox::Warning, tr("Could not open repository"), message, QMessageBox::Ok,
                    this);
    if (!detail.isEmpty()) {
        box.setDetailedText(detail);
    }
    box.exec();
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
    // second click.
    if (detail && !detail->files.empty()) {
        onFileActivated(QString::fromStdString(detail->files.front().path));
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

void MainWindow::onStatusReady(gity::session::StatusPtr status) {
    if (status) {
        changedFiles_ = static_cast<int>(status->entries.size());
        refs_->setLocalChangeCount(changedFiles_);
    }
    currentStatus_ = status;
    changes_->setStatus(status);
}

void MainWindow::onCommitContextMenu(qsizetype row, const QPoint& globalPos) {
    const QString oid = model_.fullId(static_cast<std::size_t>(row));
    if (oid.isEmpty()) {
        return;
    }
    const QString shortId = oid.left(8);

    using gity::ui::icons::Glyph;
    // Menu text, so the icons sit at the same weight as the words beside them.
    const auto glyph = [](Glyph which) { return gity::ui::icons::themed(which); };

    QMenu menu(this);
    // Both verbs act on the commit under the cursor, so both name it. A menu
    // that says only "Revert" leaves the user checking which row was selected.
    QAction* pick = menu.addAction(glyph(Glyph::CherryPick),
                                   tr("Cherry-pick %1 onto this branch").arg(shortId));
    QAction* revert = menu.addAction(glyph(Glyph::Revert), tr("Revert %1").arg(shortId));
    menu.addSeparator();
    QAction* rebase =
        menu.addAction(glyph(Glyph::Rebase), tr("Rebase the commits above %1…").arg(shortId));
    menu.addSeparator();
    QAction* tagHere = menu.addAction(glyph(Glyph::Tag), tr("Tag %1…").arg(shortId));
    QAction* reset =
        menu.addAction(glyph(Glyph::Reset), tr("Reset this branch to %1…").arg(shortId));
    menu.addSeparator();
    QAction* copy = menu.addAction(glyph(Glyph::Copy), tr("Copy Commit SHA"));

    using gity::ui::describeCommand;
    menu.setToolTipsVisible(true);
    describeCommand(pick, tr("Apply this commit's change on top of the current branch."),
                    QStringLiteral("cherry-pick %1").arg(shortId));
    describeCommand(revert, tr("Add a new commit that undoes this one."),
                    QStringLiteral("revert %1").arg(shortId));
    describeCommand(rebase, tr("Reorder, squash, edit or drop the commits after this one."),
                    QStringLiteral("rebase -i %1").arg(shortId));
    describeCommand(tagHere, tr("Name this commit."), QStringLiteral("tag <name> %1").arg(shortId));
    describeCommand(reset, tr("Move the current branch to this commit."),
                    QStringLiteral("reset --soft|--mixed|--hard %1").arg(shortId));

    QAction* chosen = menu.exec(globalPos);
    if (chosen == nullptr) {
        return;
    }
    if (chosen == copy) {
        QGuiApplication::clipboard()->setText(oid);
        return;
    }
    if (chosen == pick) {
        session()->requestCherryPick(oid);
        return;
    }
    if (chosen == revert) {
        // Revert writes a commit; cherry-pick does too. Neither rewrites
        // history, so neither is confirmed — both are undone by reverting
        // again or resetting, and a confirmation on every ordinary action
        // teaches people to click through them.
        session()->requestRevert(oid);
        return;
    }
    if (chosen == rebase) {
        startRebase(oid, model_.summary(static_cast<std::size_t>(row)));
        return;
    }
    if (chosen == tagHere) {
        tagTarget_ = oid;
        chooseNewTag();
        return;
    }
    if (chosen == reset) {
        confirmReset(oid, model_.summary(static_cast<std::size_t>(row)));
    }
}

void MainWindow::chooseNewTag() {
    if (currentWorkdir_.isEmpty()) {
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("New Tag"));
    dialog.setMinimumWidth(420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;

    auto* name = new QLineEdit(&dialog);
    name->setPlaceholderText(QStringLiteral("v1.0.0"));
    form->addRow(tr("Name"), name);

    auto* message = new QLineEdit(&dialog);
    message->setPlaceholderText(tr("Optional — leave empty for a lightweight tag"));
    form->addRow(tr("Message"), message);
    layout->addLayout(form);

    auto* note = new QLabel(
        tr("A message makes this an annotated tag: an object of its own carrying who made it "
           "and when, which is what a release wants. Without one it is a bare pointer at the "
           "commit.\n\nTags are not pushed by Push — right-click the tag to send it."),
        &dialog);
    note->setWordWrap(true);
    gity::ui::applyRole(note, QStringLiteral("note"));
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted || name->text().trimmed().isEmpty()) {
        return;
    }
    session()->requestCreateTag(name->text().trimmed(), tagTarget_, message->text().trimmed());
    tagTarget_.clear();
}

void MainWindow::confirmReset(const QString& oid, const QString& subject) {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Reset Branch"));
    dialog.setMinimumWidth(520);
    auto* layout = new QVBoxLayout(&dialog);

    auto* intro = new QLabel(
        tr("Move this branch to %1 — \"%2\".\n\nCommits after it stay reachable through the "
           "reflog, so this is recoverable; what it does to your working copy is not.")
            .arg(oid.left(8), subject),
        &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // Each mode says what happens to the two things people actually care
    // about, because "soft, mixed, hard" means nothing until you have been
    // bitten by picking the wrong one.
    auto* soft = new QRadioButton(tr("Soft — keep all changes, staged"), &dialog);
    auto* mixed = new QRadioButton(tr("Mixed — keep all changes, unstaged"), &dialog);
    auto* hard = new QRadioButton(tr("Hard — discard all changes"), &dialog);
    mixed->setChecked(true);
    gity::ui::applyRole(hard, QStringLiteral("error"));
    layout->addWidget(soft);
    layout->addWidget(mixed);
    layout->addWidget(hard);

    auto* warning = new QLabel(&dialog);
    warning->setWordWrap(true);
    gity::ui::applyRole(warning, QStringLiteral("warn"));
    layout->addWidget(warning);
    const auto updateWarning = [warning, hard] {
        warning->setText(hard->isChecked()
                             ? tr("Uncommitted work in the working copy is thrown away. Gity "
                                  "snapshots it first, so Edit ▸ Undo brings it back — but "
                                  "nothing outside Gity can.")
                             : QString());
    };
    connect(hard, &QRadioButton::toggled, &dialog, updateWarning);
    updateWarning();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    QPushButton* confirm = buttons->addButton(tr("Reset"), QDialogButtonBox::AcceptRole);
    // Reset is only destructive in its hard mode, so the button follows the
    // choice rather than shouting at someone doing a soft one.
    const auto markMode = [confirm, hard] {
        gity::ui::applyRole(confirm, hard->isChecked() ? QStringLiteral("destructive")
                                                       : QStringLiteral("primary"));
    };
    connect(hard, &QRadioButton::toggled, &dialog, markMode);
    markMode();
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    session()->requestReset(oid, soft->isChecked()   ? QStringLiteral("soft")
                                 : hard->isChecked() ? QStringLiteral("hard")
                                                     : QStringLiteral("mixed"));
}

void MainWindow::startRebase(const QString& baseOid, const QString& baseSubject) {
    gity::ui::RebaseDialog dialog(*session(), baseOid, baseSubject, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    // Rebase rewrites commits, unlike every other verb offered here. It gets a
    // confirmation, and the confirmation says the thing that matters: shared
    // commits become different commits, and anyone who has them keeps the old
    // ones.
    const auto answer = QMessageBox::question(
        this, tr("Rewrite these commits?"),
        tr("A rebase replaces the commits above %1 with new ones. If any of them have been "
           "pushed, everyone else keeps the originals and the two histories diverge.\n\n"
           "Nothing is lost either way — the old commits stay reachable through the reflog.")
            .arg(baseOid.left(8)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    session()->requestInteractiveRebase(dialog.baseOid(), dialog.steps());
}

void MainWindow::openSubmodule(const QString& relativePath) {
    if (currentWorkdir_.isEmpty()) {
        return;
    }
    const QString child = QDir(currentWorkdir_).filePath(relativePath);
    if (!QFileInfo::exists(child)) {
        QMessageBox::information(
            this, tr("Submodule not present"),
            tr("%1 is recorded but its files are not on disk. Initialise it with "
               "git submodule update --init before it can be opened.")
                .arg(relativePath));
        return;
    }

    // The tab is added first, carrying the parent's name, so openRepository
    // finds it and treats this as a switch.
    repoTabs_->open(child, QDir(currentWorkdir_).dirName());
    openRepository(child);
}

void MainWindow::updateRepositoryTitle() {
    // Set here and nowhere else: this used to be written again when refs
    // arrived, which landed later and quietly dropped the "submodule of" part.
    const QString current = repoTabs_->currentPath();
    const QString parent = repoTabs_->currentParentName();
    if (current.isEmpty()) {
        setWindowTitle(tr("Gity"));
        return;
    }
    if (parent.isEmpty()) {
        setWindowTitle(tr("%1 — Gity").arg(QDir(current).dirName()));
        return;
    }
    setWindowTitle(tr("%1 — submodule of %2 — Gity").arg(QDir(current).dirName(), parent));
}

gity::session::SwitchContext MainWindow::switchContext(const QString& ref) const {
    return gity::session::switchContext(currentRefs_.get(), currentStatus_.get(), ref);
}

void MainWindow::confirmCheckout(const QString& ref) {
    if (currentWorkdir_.isEmpty()) {
        return;
    }
    // What is being switched to: a remote branch becomes a local one tracking
    // it; a tag or commit is checked out detached.
    const gity::session::SwitchContext context = switchContext(ref);

    // Nothing to decide: switch, and bring submodules along.
    if (changedFiles_ == 0) {
        session()->requestCheckout(ref, context.kind, gity::session::LocalChanges::Keep,
                                   context.hasSubmodules);
        return;
    }
    // Asked before switching rather than discovered after: what to do with
    // the changes, and whether submodules follow the branch.
    gity::ui::CheckoutDialog dialog(
        ref, context, changedFiles_,
        currentRefs_ ? QString::fromStdString(currentRefs_->headName) : QString(), this);
    if (dialog.exec() == QDialog::Accepted) {
        session()->requestCheckout(ref, context.kind, dialog.changes(), dialog.updateSubmodules());
    }
}

void MainWindow::openNewBranchDialog(const QString& base) {
    if (currentWorkdir_.isEmpty()) {
        return;
    }
    const gity::session::SwitchContext context = switchContext(base);
    QStringList existing;
    if (currentRefs_) {
        for (const auto& branch : currentRefs_->localBranches) {
            existing << QString::fromStdString(branch.name);
        }
    }
    gity::ui::NewBranchDialog dialog(base, context, changedFiles_, existing, this);
    if (dialog.exec() == QDialog::Accepted) {
        session()->requestCreateBranch(dialog.name(), base, context.kind, dialog.switchesTo(),
                                       dialog.changes(), dialog.updateSubmodules());
    }
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

void MainWindow::chooseStash() {
    if (currentWorkdir_.isEmpty()) {
        return;
    }
    if (changedFiles_ == 0) {
        QMessageBox::information(this, tr("Nothing to stash"),
                                 tr("The working copy has no changes."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Stash Changes"));
    dialog.setMinimumWidth(460);
    auto* layout = new QVBoxLayout(&dialog);

    auto* label = new QLabel(tr("Set the changes aside and return the working copy to the "
                                "last commit. Nothing is lost — they come back with Pop."),
                             &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto* message = new QLineEdit(&dialog);
    message->setPlaceholderText(tr("Description (optional)"));
    layout->addWidget(message);

    auto* untracked = new QCheckBox(tr("Include new, untracked files"), &dialog);
    // On by default: a stash that leaves new files behind is not the whole
    // change, and finding that out later is the expensive way to learn it.
    untracked->setChecked(true);
    layout->addWidget(untracked);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    QPushButton* save = buttons->addButton(tr("Stash"), QDialogButtonBox::AcceptRole);
    Q_UNUSED(save)
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    session()->requestStashSave(message->text().trimmed(), untracked->isChecked());
}

void MainWindow::chooseNewBranch() {
    openNewBranchDialog(QString());
}

void MainWindow::chooseComparison() {
    if (currentWorkdir_.isEmpty() || !currentRefs_) {
        return;
    }
    // Every branch and tag, the checked-out branch first.
    QStringList names;
    QStringList oids;
    const auto add = [&names, &oids](const std::vector<gity::git::RefEntry>& entries) {
        for (const auto& entry : entries) {
            char buffer[GIT_OID_SHA1_HEXSIZE + 1] = {};
            git_oid_tostr(buffer, sizeof(buffer), &entry.target);
            names << QString::fromStdString(entry.name);
            oids << QString::fromLatin1(buffer);
        }
    };
    add(currentRefs_->localBranches);
    add(currentRefs_->remoteBranches);
    add(currentRefs_->tags);
    if (names.size() < 2) {
        QMessageBox::information(this, tr("Compare Branches"),
                                 tr("There is only one branch, so nothing to compare it with."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Compare Branches"));
    dialog.setMinimumWidth(440);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* from = new QComboBox(&dialog);
    auto* to = new QComboBox(&dialog);
    from->addItems(names);
    to->addItems(names);
    from->setCurrentIndex(0);
    to->setCurrentIndex(1);
    form->addRow(tr("From"), from);
    form->addRow(tr("To"), to);
    layout->addLayout(form);
    auto* note = new QLabel(tr("Shows what changes going from the first to the second — tip to "
                               "tip — and the commits each has that the other does not."),
                            &dialog);
    note->setWordWrap(true);
    gity::ui::applyRole(note, QStringLiteral("note"));
    layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    QPushButton* compare = buttons->addButton(tr("Compare"), QDialogButtonBox::AcceptRole);
    gity::ui::applyRole(compare, QStringLiteral("primary"));
    compare->setDefault(true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || from->currentIndex() == to->currentIndex()) {
        return;
    }
    showScreen(gity::ui::RefsSidebar::Screen::History);
    startComparison(names.at(from->currentIndex()), oids.at(from->currentIndex()),
                    names.at(to->currentIndex()), oids.at(to->currentIndex()));
}

void MainWindow::chooseMerge() {
    if (currentWorkdir_.isEmpty() || !refs_) {
        return;
    }
    const gity::session::RefSetPtr refs = currentRefs_;
    if (!refs) {
        return;
    }

    QStringList candidates;
    QString head;
    for (const auto& branch : refs->localBranches) {
        if (branch.isHead) {
            head = QString::fromStdString(branch.name);
            continue; // merging a branch into itself is not an offer worth making
        }
        candidates << QString::fromStdString(branch.name);
    }
    for (const auto& branch : refs->remoteBranches) {
        candidates << QString::fromStdString(branch.name);
    }
    if (candidates.isEmpty()) {
        QMessageBox::information(this, tr("Nothing to merge"),
                                 tr("This repository has no other branch to merge from."));
        return;
    }

    bool accepted = false;
    const QString chosen =
        QInputDialog::getItem(this, tr("Merge"), tr("Merge which branch into %1?").arg(head),
                              candidates, 0, false, &accepted);
    if (!accepted || chosen.isEmpty()) {
        return;
    }
    askPreview(gity::session::Preview::Kind::Merge, chosen);
}

void MainWindow::openSettings(SettingsPage page) {
    // The origin URL is what the Accounts section binds to an account, so it
    // is looked up now rather than remembered: a remote can be changed from
    // the Remotes dialog while this window is open.
    QString originUrl;
    if (currentRemotes_) {
        for (const auto& remote : *currentRemotes_) {
            if (remote.name == "origin") {
                originUrl = QString::fromStdString(remote.fetchUrl);
                break;
            }
        }
        if (originUrl.isEmpty() && !currentRemotes_->empty()) {
            originUrl = QString::fromStdString(currentRemotes_->front().fetchUrl);
        }
    }

    QList<QPair<QString, QString>> remotes;
    if (currentRemotes_) {
        for (const auto& remote : *currentRemotes_) {
            remotes.append(
                {QString::fromStdString(remote.name), QString::fromStdString(remote.fetchUrl)});
        }
    }
    gity::ui::SettingsDialog dialog(currentWorkdir_, originUrl, remotes, this);
    // One way to choose a repository's account: its own credential config,
    // for it and its submodules (RepoWorker::setAccountChoice). Accounts'
    // "Use for this repository" used to rewrite the remote URL instead, which
    // GitHub CLI ignores for any account but its active one.
    const auto assign = [this](const QString& account) { session()->requestSetAccount(account); };
    connect(&dialog, &gity::ui::SettingsDialog::useAccountForRepository, this, assign);
    connect(&dialog, &gity::ui::SettingsDialog::repositoryAccountChosen, this, assign);
    dialog.setRepositoryAccount(account_.host, account_.viaGitHubCli, account_.accounts,
                                account_.defaultAccount, account_.chosen);
    if (page == SettingsPage::Accounts) {
        dialog.showAccounts();
    } else if (page == SettingsPage::Credentials) {
        dialog.showCredentials();
    } else if (page == SettingsPage::Appearance) {
        dialog.showAppearance();
    }
    dialog.exec();
}

void MainWindow::chooseInteractiveRebase() {
    if (currentWorkdir_.isEmpty()) {
        return;
    }
    // The bases come from git, not from the graph: the graph's rows include
    // every other branch, and a filter's matches, and a base that is not an
    // ancestor of HEAD rebases nothing the user meant.
    awaitingRebaseBases_ = true;
    session()->requestHeadAncestors(31);
}

void MainWindow::offerInteractiveRebase(const QStringList& oids, const QStringList& subjects) {
    if (!awaitingRebaseBases_) {
        return;
    }
    awaitingRebaseBases_ = false;

    // Row 0 is HEAD itself, which has nothing above it to rebase.
    QStringList choices;
    std::vector<qsizetype> rows;
    for (qsizetype row = 1; row < oids.size(); ++row) {
        choices << tr("%1  %2").arg(oids.at(row).left(7), subjects.at(row));
        rows.push_back(row);
    }
    if (choices.isEmpty()) {
        QMessageBox::information(
            this, tr("Nothing to rebase"),
            tr("There is only one commit, so there is nothing above it to reorder."));
        return;
    }

    // "Rebase everything above this one" is the question an interactive
    // rebase actually answers, so the base is picked rather than counted.
    bool accepted = false;
    const QString chosen = QInputDialog::getItem(
        this, tr("Interactive Rebase"),
        tr("Rebase the commits above which one?\n"
           "Everything newer than the commit you pick can be reordered, squashed or dropped."),
        choices, 0, false, &accepted);
    if (!accepted || chosen.isEmpty()) {
        return;
    }
    const qsizetype index = choices.indexOf(chosen);
    if (index < 0) {
        return;
    }
    const qsizetype row = rows[static_cast<std::size_t>(index)];
    startRebase(oids.at(row), subjects.at(row));
}

void MainWindow::chooseRebase() {
    if (currentWorkdir_.isEmpty() || !currentRefs_) {
        return;
    }

    QStringList candidates;
    QString head;
    for (const auto& branch : currentRefs_->localBranches) {
        if (branch.isHead) {
            head = QString::fromStdString(branch.name);
            continue;
        }
        candidates << QString::fromStdString(branch.name);
    }
    for (const auto& branch : currentRefs_->remoteBranches) {
        candidates << QString::fromStdString(branch.name);
    }
    if (candidates.isEmpty()) {
        QMessageBox::information(this, tr("Nothing to rebase onto"),
                                 tr("This repository has no other branch to rebase onto."));
        return;
    }

    bool accepted = false;
    const QString onto =
        QInputDialog::getItem(this, tr("Rebase"), tr("Replay %1 on top of which branch?").arg(head),
                              candidates, 0, false, &accepted);
    if (!accepted || onto.isEmpty()) {
        return;
    }

    askPreview(gity::session::Preview::Kind::Rebase, onto);
}

void MainWindow::askPreview(gity::session::Preview::Kind kind, const QString& target,
                            const QString& branch) {
    if (currentWorkdir_.isEmpty()) {
        return;
    }
    statusBar()->showMessage(kind == gity::session::Preview::Kind::Pull
                                 ? tr("Checking the remote…")
                                 : tr("Working out what will happen…"));
    session()->requestPreview(kind, target, branch);
}

void MainWindow::showPreview(gity::session::PreviewPtr preview) {
    using Kind = gity::session::Preview::Kind;
    using gity::ui::PreviewChoice;
    statusBar()->clearMessage();
    if (!preview) {
        return;
    }
    const auto& p = *preview;
    switch (gity::ui::choosePreviewAction(p, this)) {
    case PreviewChoice::Go:
        if (p.kind == Kind::Pull) {
            session()->requestPull();
        } else if (p.kind == Kind::Merge) {
            session()->requestMerge(p.target);
        } else if (p.kind == Kind::Rebase) {
            session()->requestRebaseOnto(p.target);
        } else {
            // The checked-out branch goes by plain `git push`, so push.default
            // and pushRemote decide as in a terminal; any other branch is
            // pushed by name.
            bool current = false;
            if (currentRefs_) {
                for (const auto& branch : currentRefs_->localBranches) {
                    current = current ||
                              (branch.isHead && QString::fromStdString(branch.name) == p.branch);
                }
            }
            if (current && !p.newBranch) {
                session()->requestPush();
            } else {
                session()->requestPushRef(p.branch);
            }
        }
        break;
    case PreviewChoice::MergeInstead:
        session()->requestMerge(p.target);
        break;
    case PreviewChoice::RebaseInstead:
        session()->requestRebaseOnto(p.target);
        break;
    case PreviewChoice::Force:
        session()->requestForcePush(p.branch, p.remote, p.remoteBranch, p.leaseOid);
        break;
    case PreviewChoice::Cancel:
        break;
    }
}

void MainWindow::onHistoryEditFinished(const QString& verb, bool ok, bool conflicted,
                                       const QString& message) {
    // A branch that holds commits nothing else references cannot be deleted
    // with -d, and git says so. Refusing to go further was the wrong call:
    // deleting a scratch branch is routine, and a client that can only report
    // the refusal makes the user go to a terminal to finish an ordinary job.
    // The safety is in making it an informed choice, not in blocking it.
    if (!ok && !pendingBranchDelete_.isEmpty() &&
        message.contains(QStringLiteral("not fully merged"))) {
        const QString name = pendingBranchDelete_;
        pendingBranchDelete_.clear();

        if (gity::ui::confirmDestructive(
                this, tr("Delete anyway?"),
                tr("\"%1\" holds commits that are not on any other branch.").arg(name),
                tr("Deleting it leaves those commits reachable only through the reflog, "
                   "which expires — after that they are gone. If you meant to keep the "
                   "work, merge it somewhere first."),
                tr("Delete Anyway"))) {
            session()->requestDeleteBranch(name, true);
        }
        return;
    }
    pendingBranchDelete_.clear();

    if (ok) {
        statusBar()->showMessage(message.isEmpty() ? tr("%1 finished").arg(verb) : message, 8000);
        openRepository(currentWorkdir_);
        return;
    }
    if (conflicted) {
        // The banner already says what to do and stays until it is resolved,
        // so a modal here would only be a second thing to dismiss.
        statusBar()->showMessage(message, 12000);
        return;
    }
    QMessageBox::warning(this, tr("%1 failed").arg(verb), message);
}

void MainWindow::chooseClone() {
    gity::ui::CloneDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    session()->requestClone(dialog.url(), dialog.parentDirectory(), dialog.folderName());
}

void MainWindow::onNetworkProgress(const QString& phase, int percent) {
    // Transient: the next phase overwrites it, and the finish message replaces
    // it entirely. A progress line that outlives the operation is a lie.
    statusBar()->showMessage(percent >= 0 ? tr("%1 %2%").arg(phase).arg(percent) : phase);
}

void MainWindow::onNetworkStarted(const QString& verb) {
    syncNetworkState();
    statusBar()->showMessage(tr("%1…").arg(verb));
}

void MainWindow::onExternalChange(int kinds) {
    if (currentWorkdir_.isEmpty()) {
        return;
    }
    // Gity's own command is running: it re-reads the repository when it is
    // done, and reading now would only show it half way.
    if (session()->busy()) {
        return;
    }
    using Change = gity::session::RepoWatcher::Change;
    if ((kinds & (Change::Refs | Change::Head)) != 0) {
        // A branch moved or HEAD changed: the history and the sidebar are
        // stale, and a refresh keeps the selected commit and the draft.
        statusBar()->showMessage(tr("The repository changed outside Gity — refreshed."), 4000);
        openRepository(currentWorkdir_);
        return;
    }
    // Files or what is staged: the status is enough, and the Changes screen
    // re-reads the diff of the file it is showing.
    session()->requestStatus();
}

void MainWindow::syncNetworkState() {
    const bool open = !currentWorkdir_.isEmpty();
    const bool busy = session()->networkBusy();
    for (QAction* action : repositoryMenuActions_) {
        action->setEnabled(open);
    }
    for (QAction* action : networkMenuActions_) {
        action->setEnabled(open && !busy);
    }
    for (QAction* action : {fetchAction_, pullAction_, pushAction_}) {
        action->setEnabled(open && !busy);
    }
    cancelNetwork_->setVisible(busy);
}

void MainWindow::reportBackgroundNetwork(const QString& path, const QString& verb, bool ok,
                                         const QString& summary) {
    // Said, not shown in a dialog: the repository is not on screen, and a
    // dialog about it would interrupt whatever is.
    const QString name = QDir(path).dirName();
    statusBar()->showMessage(ok ? tr("%1: %2 — %3").arg(name, verb, summary)
                                : tr("%1: %2 failed — %3").arg(name, verb, summary),
                             12000);
}

void MainWindow::activateSession(const QString& path) {
    if (!sessions_.activate(path)) {
        return;
    }
    // Nothing is known about the arriving repository until it reports.
    currentWorkdir_.clear();
    syncNetworkState();
}

void MainWindow::retireSession(const QString& path) {
    viewStates_.remove(gity::ui::RepositoryTabs::canonical(path));
    sessions_.retire(path);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    const QStringList busy = sessions_.busyRepositories();
    if (!busy.isEmpty()) {
        const auto answer = QMessageBox::warning(
            this, tr("git is still working"),
            tr("git is still changing %1. Quit anyway?").arg(busy.join(QStringLiteral(", "))),
            QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel);
        // Quitting stops a fetch, pull, push or clone where it is. A commit,
        // merge or rebase step is let finish — the window waits for it —
        // because stopping one half way leaves the repository in a state
        // nobody chose.
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        statusBar()->showMessage(tr("Waiting for git to finish…"));
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::onNetworkFinished(const QString& verb, bool ok, const QString& summary,
                                   const QString& detail) {
    syncNetworkState();

    if (ok) {
        statusBar()->showMessage(tr("%1 — %2").arg(verb, summary), 8000);
        // Refs and history both move, so reopen rather than patching in place.
        openRepository(currentWorkdir_);
        return;
    }

    QMessageBox box(QMessageBox::Warning, tr("%1 failed").arg(verb), summary, QMessageBox::Ok,
                    this);
    if (!detail.isEmpty()) {
        box.setDetailedText(detail);
    }
    box.exec();
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
    detail_->setPending(model_.shortId(index), model_.summary(index));
    setSummary(model_.summary(index),
               QStringLiteral("%1  ·  %2  ·  %3")
                   .arg(model_.author(index), model_.when(index), model_.shortId(index)));
    setChangesCount(-1);
    session()->requestDetail(currentOid_);
}

} // namespace gity::app
