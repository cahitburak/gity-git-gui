// M2 — the staging surface.
//
// Two lists, unstaged and staged, with the diff of whatever is selected and a
// commit box beneath. ADR-002 chrome: ordinary widgets, since neither list
// carries a million rows.
//
#pragma once

#include "session/RepoSession.h"

#include <QWidget>

class QLabel;
class QCheckBox;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace gity::ui {

class DiffView;

class WorkingCopyPanel : public QWidget {
    Q_OBJECT

public:
    explicit WorkingCopyPanel(QWidget* parent = nullptr);

    void setStatus(session::StatusPtr status);
    void setDiff(session::FileDiffPtr diff, session::HunkStagingPtr staging = {});

    /// LFS locks, for the `locked` chip and the context menu. Advisory: an
    /// empty list simply means nothing is known to be locked.
    void setLocks(session::LockListPtr locks, bool supported);
    void showCommitted(const QString& summary);
    void showCommitFailed(const QString& message);
    /// Empties the screen for a repository being (re)opened. `keepMessage`
    /// is for a refresh of the same repository — a fetch finishing in the
    /// background must not take a half-written commit message with it.
    void clearAll(bool keepMessage = false);

    /// The half-written commit message and the Amend tick, so they can be
    /// kept per repository while another tab is showing.
    [[nodiscard]] QString draftMessage() const;
    [[nodiscard]] bool draftAmend() const;
    void restoreDraft(const QString& message, bool amend);

    /// The repository's working directory, for opening, revealing and copying
    /// the full path of a file.
    void setWorkdir(const QString& workdir) { workdir_ = workdir; }

protected:
    /// Clicks on a row's checkbox: stage or unstage, without touching the
    /// selection the click would otherwise replace.
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void stageRequested(const QStringList& paths);
    void unstageRequested(const QStringList& paths);
    void diffRequested(const QString& path, bool staged);
    void commitRequested(const QString& message, bool amend);
    void applyPatchRequested(const QString& patchText);
    void discardHunkRequested(const QString& patchText);
    void discardRequested(const QStringList& paths);
    /// Stash just these files, leaving the rest of the working copy alone.
    void stashRequested(const QStringList& paths);
    void lockRequested(const QString& path);
    void unlockRequested(const QString& path, bool force);

private:
    void showRowMenu(QTreeWidget* list, const QPoint& where);
    QTreeWidget* buildList(const QString& title);
    void rebuild();
    void fillList(QTreeWidget* list, bool staged);
    [[nodiscard]] QStringList selectionOf(QTreeWidget* list) const;
    /// `paths` plus the old path of any that are renames on that side, so a
    /// rename is staged or unstaged whole.
    [[nodiscard]] QStringList withRenameSources(const QStringList& paths, bool staged) const;
    void updateCommitButton();
    void setNotice(const QString& text, bool isError);

    QString workdir_;
    QTreeWidget* unstaged_ = nullptr;
    QTreeWidget* staged_ = nullptr;
    QPushButton* stageButton_ = nullptr;
    QPushButton* unstageButton_ = nullptr;
    QPushButton* stageAll_ = nullptr;
    QPushButton* unstageAll_ = nullptr;
    QPushButton* commitButton_ = nullptr;
    QPushButton* stageSelectionButton_ = nullptr;
    QPlainTextEdit* message_ = nullptr;
    QLabel* notice_ = nullptr;
    QLabel* commitReason_ = nullptr;
    QLabel* stagedHeader_ = nullptr;
    QLabel* unstagedHeader_ = nullptr;
    QCheckBox* amend_ = nullptr;
    DiffView* diff_ = nullptr;

    session::StatusPtr status_;
    session::LockListPtr locks_;
    bool lockingSupported_ = false;
    bool viewingStaged_ = false;
};

} // namespace gity::ui
