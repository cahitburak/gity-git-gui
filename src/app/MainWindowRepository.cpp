// MainWindow: opening repositories, switching between them, and showing
// what the session on screen reports about the one open.
#include "MainWindow.h"

#include "SavedState.h"

#include "session/Accounts.h"
#include "session/RepoWatcher.h"
#include "ui/conflict/ConflictWindow.h"
#include "ui/graphview/CommitGraphView.h"
#include "ui/graphview/GraphHeader.h"
#include "ui/graphview/HistoryFilterBar.h"
#include "ui/panels/BlamePanel.h"
#include "ui/panels/Confirm.h"
#include "ui/panels/OperationBanner.h"
#include "ui/panels/RefsSidebar.h"
#include "ui/panels/RepositoryTabs.h"
#include "ui/panels/WelcomePanel.h"
#include "ui/panels/WorkingCopyPanel.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <algorithm>

namespace gity::app {

void MainWindow::rememberRepository(const QString& path) {
    if (!persistSession_) {
        return;
    }
    welcome_->setRecent(saved::rememberRepository(path));
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
    // Found first, all of them: each tab opened below saves the list, with
    // the unavailable ones added, and one found only after the last save
    // was dropped from it.
    unavailableTabs_.clear();
    for (const saved::Tab& tab : tabs.tabs) {
        if (!tab.path.isEmpty() && !QFileInfo::exists(tab.path + QStringLiteral("/.git")) &&
            !QFileInfo::exists(tab.path)) {
            unavailableTabs_ << tab;
        }
    }
    QString currentPath;
    for (int i = 0; i < tabs.tabs.size(); ++i) {
        const saved::Tab& tab = tabs.tabs.at(i);
        if (tab.path.isEmpty() || !QFileInfo::exists(tab.path + QStringLiteral("/.git"))) {
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
    // The conflict window lets go of the repository being left; it takes up
    // the new one in onOpened, under the workdir the session settles on — not
    // `path`, which may be relative, a subfolder or a symlink, and would
    // differ from the spelling every later refresh uses.
    if (!refreshing) {
        conflicts_->setRepository({}, {});
    }

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
        // A blame is of one repository's file.
        blame_->clear();
        // What was last reported about the repository being left: its refs,
        // remotes, status and account, which dialogs ask questions from until
        // the arriving one's come in. The branch filter was typed for its
        // branches.
        currentRefs_.reset();
        currentRemotes_.reset();
        currentStatus_.reset();
        account_ = {};
        accountStatus_->setVisible(false);
        refs_->setPullRequestNoun({});
        refFilter_->clear();
        pullAction_->setText(tr("Pull"));
        pushAction_->setText(tr("Push"));
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
    currentRenamedPath_.clear();
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
    // The commit being read stays selected if it matches, which keeps the
    // place; otherwise the newest match is selected once the walk ends, as
    // the first chunk of an unfiltered one would be. Without this nothing
    // was selected after a filter changed. One still being looked for, when
    // the filter changes again before it is found, is kept.
    if (!currentOid_.isEmpty()) {
        pendingSelectOid_ = currentOid_;
    }
    firstChunkSeen_ = false;
    // The model has to be emptied before the new walk streams into it, or
    // the filtered rows are appended to the unfiltered ones and the view
    // shows both sets at once.
    model_.clear();
    graph_->setModel(&model_);
    clearCommitView();
    currentOid_.clear();
    currentRenamedPath_.clear();
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
    // Opened through a subfolder or a symlink, a repository settles on its
    // workdir, and every refresh after this opens it by that spelling. Its
    // tab, its session and what its tab was showing are found by it from
    // now on: otherwise the first refresh found no tab, opened a second one,
    // and closing the first retired the session on screen.
    using gity::ui::RepositoryTabs;
    const QString tabPath = repoTabs_->currentPath();
    if (!info.workdir.isEmpty() && !tabPath.isEmpty() &&
        RepositoryTabs::canonical(tabPath) != RepositoryTabs::canonical(info.workdir)) {
        if (repoTabs_->contains(info.workdir)) {
            // Already open in a tab of its own: that one is brought forward
            // and this one closed — after this signal has returned, since
            // closing it destroys the session sending it.
            const QString workdir = info.workdir;
            QTimer::singleShot(0, this, [this, tabPath, workdir] {
                openRepository(workdir);
                repoTabs_->close(tabPath);
            });
            return;
        }
        sessionPaths_.insert(RepositoryTabs::canonical(info.workdir), sessions_.currentPath());
        if (viewStates_.contains(RepositoryTabs::canonical(tabPath))) {
            viewStates_.insert(RepositoryTabs::canonical(info.workdir),
                               viewStates_.take(RepositoryTabs::canonical(tabPath)));
        }
        repoTabs_->rekey(tabPath, info.workdir);
    }

    repoLabel_->setText(QStringLiteral("%1  ·  %2").arg(info.workdir, info.headRef));
    currentWorkdir_ = info.workdir;
    conflicts_->setRepository(info.workdir, QDir(info.workdir).dirName());
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
        // branch's upstream, which is the only one Pull and Push act on — and
        // with HEAD detached or unborn there is none, so no counts.
        pullAction_->setText(tr("Pull"));
        pushAction_->setText(tr("Push"));
        gity::ui::describeCommand(pushAction_, tr("Send your new commits to the remote."),
                                  QStringLiteral("push"));
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
    const auto report = [this, message, detail] {
        gity::ui::showPlainMessage(this, QMessageBox::Warning, tr("Could not open repository"),
                                   message, QMessageBox::Ok, QMessageBox::NoButton, detail);
    };
    if (sessions_.currentNeverOpened()) {
        const QString failedPath = sessions_.currentPath();
        currentWorkdir_.clear();
        repoLabel_->setText(tr("No repository open"));
        // After this signal has returned: the session sending it is about to
        // be destroyed, and must not be while it is still emitting. The
        // message waits too — a dialog's own event loop runs timers, and
        // shown here it destroyed the session under the signal anyway.
        QTimer::singleShot(0, this, [this, failedPath, report] {
            repoTabs_->close(failedPath); // may bring another tab forward
            retireSession(failedPath);
            if (repoTabs_->count() == 0) {
                tabs_->setCurrentIndex(ScreenWelcome);
            }
            report();
        });
        return;
    }
    repoLabel_->setText(currentWorkdir_.isEmpty() ? tr("No repository open") : repoLabel_->text());
    report();
}

void MainWindow::onStatusReady(gity::session::StatusPtr status) {
    if (status) {
        changedFiles_ = static_cast<int>(status->entries.size());
        refs_->setLocalChangeCount(changedFiles_);
    }
    currentStatus_ = status;
    changes_->setStatus(status);
    conflicts_->setStatus(status);
    operationBanner_->setConflictCount(status ? static_cast<int>(status->conflictedCount()) : 0);
}

void MainWindow::openSubmodule(const QString& relativePath) {
    if (currentWorkdir_.isEmpty()) {
        return;
    }
    const QString child = QDir(currentWorkdir_).filePath(relativePath);
    if (!QFileInfo::exists(child)) {
        gity::ui::showPlainMessage(
            this, QMessageBox::Information, tr("Submodule not present"),
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

QString MainWindow::sessionPathFor(const QString& path) const {
    return sessionPaths_.value(gity::ui::RepositoryTabs::canonical(path), path);
}

void MainWindow::openClone(const QString& path) {
    // Not under a dialog or a menu: each is asking about the repository on
    // screen, and switching to the clone while one is open carried its answer
    // — a reset, a branch deleted — to the clone instead.
    if (QApplication::activeModalWidget() != nullptr ||
        QApplication::activePopupWidget() != nullptr) {
        QTimer::singleShot(250, this, [this, path] { openClone(path); });
        return;
    }
    openRepository(path);
}

bool MainWindow::stillShowing(const QString& workdir) const {
    return !workdir.isEmpty() && workdir == currentWorkdir_;
}

void MainWindow::activateSession(const QString& path) {
    if (!sessions_.activate(sessionPathFor(path))) {
        return;
    }
    // Nothing is known about the arriving repository until it reports.
    currentWorkdir_.clear();
    syncNetworkState();
}

void MainWindow::retireSession(const QString& path) {
    const QString key = gity::ui::RepositoryTabs::canonical(path);
    viewStates_.remove(key);
    sessions_.retire(sessionPathFor(path));
    sessionPaths_.remove(key);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    const QStringList busy = sessions_.busyRepositories();
    if (!busy.isEmpty()) {
        const auto answer = gity::ui::showPlainMessage(
            this, QMessageBox::Warning, tr("git is still working"),
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
    if (conflicts_->hasUnsaved()) {
        const auto answer = QMessageBox::warning(
            this, tr("Conflict choices not saved"),
            tr("Choices made in the conflict editor have not been saved, and go when Gity "
               "closes. Quit anyway?"),
            QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            conflicts_->openFile();
            event->ignore();
            return;
        }
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

    gity::ui::showPlainMessage(this, QMessageBox::Warning, tr("%1 failed").arg(verb), summary,
                               QMessageBox::Ok, QMessageBox::NoButton, detail);
}

} // namespace gity::app
