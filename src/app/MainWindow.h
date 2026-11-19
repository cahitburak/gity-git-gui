#pragma once

#include "SavedState.h"
#include "session/RepoSession.h"
#include "session/SessionHub.h"
#include "session/SwitchContext.h"
#include "ui/model/HistoryModel.h"
#include "ui/panels/RefsSidebar.h"

#include <QElapsedTimer>

#include <QHash>
#include <functional>

#include <QMainWindow>
#include <memory>

#include "core/git/HistoryStream.h"
#include "core/git/Provider.h"

class QStackedWidget;
class QTabWidget;
class QToolButton;
class QSplitter;
class QDockWidget;

class QAction;
class QCloseEvent;
class QLabel;
class QLineEdit;
class QTimer;

namespace gity::ui {
class BlamePanel;
class ConflictWindow;
class ChangedFilesList;
class WelcomePanel;
class CommitDetailPanel;
class OperationBanner;
class RepositoryTabs;
class ImageDiffView;
class DiffView;
class WorkingCopyPanel;
class CommitGraphView;
class GraphHeader;
class HistoryFilterBar;
class RefsSidebar;
} // namespace gity::ui

namespace gity::app {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /// Which Settings section to open on.
    enum class SettingsPage { General, Accounts, Credentials, Appearance };

    explicit MainWindow(QWidget* parent = nullptr);

    void openRepository(const QString& path);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    /// Asks before quitting while git is changing a repository.
    void closeEvent(QCloseEvent* event) override;

public:
    /// Reopens the tabs the previous session left. Only the
    /// current tab's repository is loaded; the others load when clicked.
    /// `openCurrent` false restores the tabs alone, for when a repository
    /// given on the command line is about to be opened anyway.
    void restoreSession(bool openCurrent);
    /// Whether the open tabs are saved as they change. Off unless asked for,
    /// so a screenshot run or a test harness never overwrites the user's.
    void setSessionPersistence(bool enabled);
    void showChangesTab();
    void showImageTab(const QString& path);
    /// Narrows the history to the commits that changed `path`, a file or a
    /// folder, keeping the selected commit if it is one of them.
    void showFileHistory(const QString& path);
    /// Opens the Blame tab on `path` as of `revision`; empty is the working
    /// copy as it is on disk.
    void blameFile(const QString& path, const QString& revision);
    /// Opens the conflict editor's window on `path`, or on the first file
    /// still conflicted.
    void openConflicts(const QString& path = {});
    /// For a screenshot run: the conflict editor's layout, 0–2 as
    /// ConflictEditor::Layout, and its window.
    void setConflictLayout(int layout);
    [[nodiscard]] QWidget* conflictWindow() const;

private slots:
    void chooseRepository();
    void onOpened(const gity::session::RepoInfo& info);
    void onRefsReady(gity::session::RefSetPtr refs);
    void onDetailReady(gity::session::DetailPtr detail);
    void onFileDiffReady(gity::session::FileDiffPtr diff);
    /// Shows one of the selected commit's files in the Changes tab — its diff,
    /// or for an image, its before and after.
    void onFileActivated(const QString& path);
    void onStatusReady(gity::session::StatusPtr status);
    void chooseClone();
    void onCommitContextMenu(qsizetype row, const QPoint& globalPos);
    void onHistoryEditFinished(const QString& verb, bool ok, bool conflicted,
                               const QString& message);
    void chooseMerge();
    void chooseComparison();
    void chooseRebase();
    void chooseInteractiveRebase();
    void offerInteractiveRebase(const QStringList& oids, const QStringList& subjects);
    void confirmReset(const QString& oid, const QString& subject);
    void chooseNewTag();
    void openSettings(gity::app::MainWindow::SettingsPage page = SettingsPage::General);
    void chooseNewBranch();
    void chooseStash();
    void showScreen(gity::ui::RefsSidebar::Screen screen);
    void confirmCheckout(const QString& ref);
    /// New Branch, from `base` or (empty) the current commit: a name, whether
    /// to switch to it, and — when switching moves the working copy — what
    /// happens to uncommitted changes and submodules, as Checkout asks.
    void openNewBranchDialog(const QString& base);
    /// Opens the host's new pull request page for `remoteBranch`
    /// ("origin/feature/x"), into that remote's default branch.
    void openPullRequestPage(const QString& remoteBranch);
    /// Opens a submodule as a repository in its own right, remembering the way
    /// back.
    void openSubmodule(const QString& relativePath);
    void updateRepositoryTitle();
    void startRebase(const QString& baseOid, const QString& baseSubject);
    void onNetworkStarted(const QString& verb);
    void onNetworkProgress(const QString& phase, int percent);
    void onNetworkFinished(const QString& verb, bool ok, const QString& summary,
                           const QString& detail);
    void onWorkingDiffReady(gity::session::FileDiffPtr diff, gity::session::HunkStagingPtr staging);
    void onImageDiffReady(gity::session::ImageDiffPtr diff);
    void onChunkReady(gity::session::ChunkPtr chunk);
    void onFinished(quint64 commits, double firstChunkMs, double totalMs, bool cancelled);
    void onFailed(const QString& message, const QString& detail);
    void onRowSelected(qsizetype row);
    /// Shows what a pull, merge, rebase or push will do, and runs it if chosen.
    void showPreview(gity::session::PreviewPtr preview);
    /// Something changed outside Gity: RepoWatcher::Change flags.
    void onExternalChange(int kinds);

private:
    /// Screens, in the order they are added to the stack. Named because the
    /// indices were literals, and deleting a screen quietly repointed every
    /// literal after it at the wrong one.
    enum Screen : int { ScreenHistory = 0, ScreenChanges = 1, ScreenWelcome = 2 };

    /// The Changes and Blame tabs, under the graph.
    static constexpr int kChangesTab = 1;
    static constexpr int kBlameTab = 2;

    /// Adds `path` to the recent repositories, when the session is saved.
    void rememberRepository(const QString& path);
    /// Asks the session for a preview; showPreview takes it from there.
    void askPreview(gity::session::Preview::Kind kind, const QString& target = {},
                    const QString& branch = {});
    /// The history header's and the graph's words for the filter in force.
    void showFilterState(const gity::git::CommitFilter& filter);
    /// Walks again with the header's filter, if it differs from the last walk.
    void applyCommitFilter();
    /// Fills the author list from the history just loaded.
    void refreshContributors();
    /// Empties the Commit and Changes tabs.
    void clearCommitView();
    /// The line above the lower tabs: what they are describing.
    void setSummary(const QString& title, const QString& meta);
    /// "Changes · N"; negative for plain "Changes" while unknown.
    void setChangesCount(int files);
    /// Shows two refs side by side in the Commit and Changes tabs.
    void startComparison(const QString& fromName, const QString& fromOid, const QString& toName,
                         const QString& toOid);
    void endComparison();
    /// The widgets, and the wiring among them that only layout needs.
    void buildLayout();
    void buildMenus();
    void buildToolBar();
    void buildStatusBar();
    void connectSession();
    void connectHistory();
    void connectSidebar();
    void connectWorkingCopy();
    void connectRepositoryTabs();
    void connectFileHistory();
    void connectConflicts();

    /// Asks for a file or folder in the repository; its path from the top,
    /// or empty if none was chosen.
    [[nodiscard]] QString choosePath(bool folder);
    /// Selects the commit `id` in the graph, or says why it cannot.
    void selectCommit(const QString& id);

    /// Makes `path`'s session the one on screen, creating it on first use.
    void activateSession(const QString& path);
    /// Opens a repository a clone just made, once no dialog or menu is open.
    void openClone(const QString& path);
    /// Whether `workdir` — the repository on screen when a dialog or menu
    /// opened — still is, now it has closed.
    [[nodiscard]] bool stillShowing(const QString& workdir) const;
    /// Closes `path`'s session and forgets what its tab was showing.
    void retireSession(const QString& path);
    /// The path `path`'s session was made under: the spelling it was opened
    /// by, which differs from its workdir when that was a subfolder or a
    /// symlink.
    [[nodiscard]] QString sessionPathFor(const QString& path) const;
    /// The network actions and Cancel button, for the session on screen.
    void syncNetworkState();
    /// A repository not on screen finished a fetch, pull or push.
    void reportBackgroundNetwork(const QString& path, const QString& verb, bool ok,
                                 const QString& summary);

    /// One session per open repository, and the relay the window is wired
    /// to (ADR-004).
    gity::session::SessionHub sessions_;

    /// The session requests go to: the one on screen, or the relay.
    [[nodiscard]] gity::session::RepoSession* session() const { return sessions_.current(); }

    QToolButton* cancelNetwork_ = nullptr;
    /// The Repository, Branch and Stash menus' entries, and the network ones
    /// among them, enabled with the toolbar's.
    QList<QAction*> repositoryMenuActions_;
    QList<QAction*> networkMenuActions_;
    gity::ui::HistoryModel model_;
    gity::ui::CommitGraphView* graph_ = nullptr;
    gity::ui::GraphHeader* graphHeader_ = nullptr;
    gity::ui::RefsSidebar* refs_ = nullptr;
    gity::ui::CommitDetailPanel* detail_ = nullptr;
    /// The Blame tab.
    gity::ui::BlamePanel* blame_ = nullptr;
    /// The conflict editor's window, made once and shown when asked for.
    gity::ui::ConflictWindow* conflicts_ = nullptr;
    gity::ui::WelcomePanel* welcome_ = nullptr;
    /// The commit's files in the Changes tab, beside the diff.
    gity::ui::ChangedFilesList* commitFiles_ = nullptr;
    QTabWidget* commitTabs_ = nullptr;
    QSplitter* historySplit_ = nullptr;
    /// The graph/lower split as it last was when both halves had room.
    QList<int> restoreHistorySizes_;
    std::function<void()> historyHeaderRestore_;
    QLabel* summaryTitle_ = nullptr;
    QLabel* summaryMeta_ = nullptr;
    QDockWidget* commandLog_ = nullptr;
    /// The Changes tab's reading area: the text diff, or the image comparison.
    QStackedWidget* diffStack_ = nullptr;
    gity::ui::DiffView* diff_ = nullptr;
    gity::ui::WorkingCopyPanel* changes_ = nullptr;
    gity::ui::ImageDiffView* image_ = nullptr;
    QStackedWidget* tabs_ = nullptr;

    /// The commit whose files the diff pane is showing.
    QString currentOid_;
    /// In a renamed file's history: what the file was called in the commit
    /// shown, when that is not the name the history is of.
    QString currentRenamedPath_;
    /// Set while two refs are being compared; `currentOid_` is empty then.
    QString compareFrom_;
    QString compareTo_;
    QString compareFromName_;
    QString compareToName_;
    QString currentWorkdir_;
    int changedFiles_ = 0;

    QLabel* repoLabel_ = nullptr;
    QLabel* countLabel_ = nullptr;
    QAction* undoAction_ = nullptr;
    QToolButton* accountStatus_ = nullptr;

    /// This repository's account, as last reported by the session.
    struct AccountChoice {
        QString host;
        bool viaGitHubCli = false;
        QStringList accounts;
        QString defaultAccount;
        QString chosen;
    };

    AccountChoice account_;
    QAction* redoAction_ = nullptr;
    QAction* fetchAction_ = nullptr;
    QAction* pullAction_ = nullptr;
    QAction* pushAction_ = nullptr;
    QAction* mergeAction_ = nullptr;
    QAction* rebaseAction_ = nullptr;
    QAction* branchAction_ = nullptr;
    QAction* stashAction_ = nullptr;
    gity::ui::OperationBanner* operationBanner_ = nullptr;
    gity::ui::RepositoryTabs* repoTabs_ = nullptr;
    QLineEdit* refFilter_ = nullptr;
    gity::ui::HistoryFilterBar* filterBar_ = nullptr;
    /// What the history on screen was walked with.
    gity::git::CommitFilter walkedFilter_;
    /// Kept so the merge chooser can list branches without asking again.
    gity::session::RefSetPtr currentRefs_;
    gity::session::RemoteListPtr currentRemotes_;
    /// The commit a tag is being created at, between menu and dialog.
    QString tagTarget_;
    /// The branch a delete was asked for, so an "unmerged" refusal can offer
    /// to force without asking which branch it meant.
    QString pendingBranchDelete_;

    bool persistSession_ = false;
    /// Interactive Rebase was chosen and its list of bases is on its way.
    bool awaitingRebaseBases_ = false;
    /// Saved tabs whose folder could not be found at start-up — a drive not
    /// mounted — kept in the saved list so they are not forgotten.
    QList<saved::Tab> unavailableTabs_;

    /// What a repository was showing, kept while another tab is on screen.
    struct ViewState {
        QString selectedOid;
        gity::git::CommitFilter filter;
        QString draft;
        bool amend = false;
        int screen = 0;
        int lowerTab = 1;
    };

    QHash<QString, ViewState> viewStates_;
    /// By workdir, canonically: the path its session was made under, where
    /// the two differ (sessionPathFor).
    QHash<QString, QString> sessionPaths_;
    /// A quit was cancelled for conflict choices not saved in a repository
    /// not on screen: the conflict window opens on them once it has opened.
    bool openUnsavedConflictsOnOpen_ = false;
    /// The working copy as last reported, for questions a dialog must ask.
    gity::session::StatusPtr currentStatus_;

    /// What switching to `ref` involves, from the loaded refs and status.
    [[nodiscard]] gity::session::SwitchContext switchContext(const QString& ref) const;
    /// The host's provider: by name, or else as chosen for an account on it.
    [[nodiscard]] static gity::git::Provider providerFor(const std::string& remoteUrl);
    /// A commit to select once the streaming history reaches it.
    QString pendingSelectOid_;
    QElapsedTimer sinceOpen_;
    bool firstChunkSeen_ = false;
};

} // namespace gity::app
