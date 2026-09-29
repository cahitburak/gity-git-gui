#pragma once

#include "session/RepoSession.h"
#include "ui/panels/RefsSidebar.h"
#include "ui/model/HistoryModel.h"

#include <QElapsedTimer>

#include <functional>
#include <QHash>
#include <QMainWindow>
#include <QSettings>

#include "core/git/Provider.h"
#include "core/git/HistoryStream.h"

class QStackedWidget;
class QTabWidget;
class QToolButton;
class QComboBox;
class QSplitter;
class QHBoxLayout;
class QDockWidget;

class QAction;
class QLabel;
class QLineEdit;
class QTimer;

namespace gity::ui {
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
class RefsSidebar;
}

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
    void onWorkingDiffReady(gity::session::FileDiffPtr diff,
                            gity::session::HunkStagingPtr staging);
    void onImageDiffReady(gity::session::ImageDiffPtr diff);
    void onChunkReady(gity::session::ChunkPtr chunk);
    void onFinished(quint64 commits, double firstChunkMs, double totalMs, bool cancelled);
    void onFailed(const QString& message, const QString& detail);
    void onRowSelected(qsizetype row);
    /// Shows what a pull, merge, rebase or push will do, and runs it if chosen.
    void showPreview(gity::session::PreviewPtr preview);

private:
    /// Repositories opened in their own right, most recent first.
    [[nodiscard]] QStringList recentRepositories() const;
    void rememberRepository(const QString& path);
    /// Asks the session for a preview; showPreview takes it from there.
    void askPreview(gity::session::Preview::Kind kind, const QString& target = {},
                    const QString& branch = {});
    /// The history header's words for the filter in force.
    void showFilterState(const gity::git::CommitFilter& filter);
    /// The filter the header's controls describe.
    [[nodiscard]] gity::git::CommitFilter currentFilter() const;
    /// Puts `filter` into the header's controls without walking.
    void showFilterWidgets(const gity::git::CommitFilter& filter);
    /// Walks again with the header's filter, if it differs from the last walk.
    void applyCommitFilter();
    void refreshDateFilterLabel();
    void chooseDateRange();
    /// Fills the author list from the history just loaded.
    void refreshContributors();
    /// Empties the Commit and Changes tabs.
    void clearCommitView();
    /// The line above the lower tabs: what they are describing.
    void setSummary(const QString& title, const QString& meta);
    /// "Changes · N"; negative for plain "Changes" while unknown.
    void setChangesCount(int files);
    /// Shows two refs side by side in the Commit and Changes tabs.
    void startComparison(const QString& fromName, const QString& fromOid,
                         const QString& toName, const QString& toOid);
    void endComparison();
    void buildMenus();
    void buildToolBar();
    void buildStatusBar();

    gity::session::RepoSession session_;
    gity::ui::HistoryModel model_;
    gity::ui::CommitGraphView* graph_ = nullptr;
    gity::ui::GraphHeader* graphHeader_ = nullptr;
    gity::ui::RefsSidebar* refs_ = nullptr;
    gity::ui::CommitDetailPanel* detail_ = nullptr;
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
    QLineEdit* commitFilter_ = nullptr;
    QComboBox* authorFilter_ = nullptr;
    QToolButton* dateFilter_ = nullptr;
    qint64 filterSince_ = 0;
    qint64 filterUntil_ = 0;
    /// What the history on screen was walked with.
    gity::git::CommitFilter walkedFilter_;
    QHBoxLayout* historyHeaderLayout_ = nullptr;
    QLabel* historyTitle_ = nullptr;
    QLabel* historyNote_ = nullptr;
    QTimer* filterDebounce_ = nullptr;
    /// True while a tab switch is driving openRepository, so the tab bar is
    /// not asked to reset itself half way through.
    bool switchingRepository_ = false;
    /// Kept so the merge chooser can list branches without asking again.
    gity::session::RefSetPtr currentRefs_;
    gity::session::RemoteListPtr currentRemotes_;
    /// The commit a tag is being created at, between menu and dialog.
    QString tagTarget_;
    /// The branch a delete was asked for, so an "unmerged" refusal can offer
    /// to force without asking which branch it meant.
    QString pendingBranchDelete_;

    bool persistSession_ = false;

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
    /// The working copy as last reported, for questions a dialog must ask.
    gity::session::StatusPtr currentStatus_;

    /// What switching to a ref involves, read from the loaded refs and status.
    struct SwitchContext {
        gity::session::RepoSession::RefKind kind = gity::session::RepoSession::RefKind::Detached;
        bool hasSubmodules = false;
        int submoduleChanges = 0;
        /// The ref is at the commit already checked out (or is empty: HEAD).
        bool sameCommit = false;
    };
    [[nodiscard]] SwitchContext switchContext(const QString& ref) const;
    /// The branches pinned in `workdir`'s sidebar, as full ref names.
    [[nodiscard]] static QStringList pinnedBranches(const QString& workdir);
    /// The host's provider: by name, or else as chosen for an account on it.
    [[nodiscard]] static gity::git::Provider providerFor(const std::string& remoteUrl);
    /// Every repository's pins, as stored: (working directory, full ref names).
    [[nodiscard]] static QList<QPair<QString, QStringList>> readPinned(QSettings& settings);
    /// A commit to select once the streaming history reaches it.
    QString pendingSelectOid_;
    QElapsedTimer sinceOpen_;
    bool firstChunkSeen_ = false;
};

} // namespace gity::app
