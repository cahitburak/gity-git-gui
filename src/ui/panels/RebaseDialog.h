// Interactive rebase, without the text editor.
//
// `git rebase -i` collects its instructions by opening an editor on a todo
// file. This collects the same instructions from a list the user can reorder
// and re-label, and hands the result to git through GIT_SEQUENCE_EDITOR.
//
// The list is shown newest first, as history is read everywhere else in this
// client. git applies it oldest first. That inversion happens in one place —
// core/git/RebaseTodo.cpp — and nowhere near this dialog.
//
// The selected commit's changes are shown underneath, so deciding what to
// squash or drop does not mean remembering what each commit did.
#pragma once

#include "core/git/RebaseTodo.h"
#include "session/RepoSession.h"

#include <QDialog>
#include <QHash>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace gity::ui {

class ChangedFilesList;
class DiffView;

class RebaseDialog : public QDialog {
    Q_OBJECT

public:
    RebaseDialog(session::RepoSession& session, const QString& baseOid,
                 const QString& baseSubject, QWidget* parent = nullptr);

    /// The instruction list, newest first. Empty until the dialog is accepted.
    [[nodiscard]] std::vector<git::RebaseStep> steps() const;
    [[nodiscard]] QString baseOid() const { return baseOid_; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setCommits(const QStringList& oids, const QStringList& subjects);
    void move(int delta);
    void revalidate();
    void applyActionToSelection(git::RebaseAction action);
    void setAction(QTreeWidgetItem* item, git::RebaseAction action);
    /// Asks for the new message of the current commit and marks it Reword.
    void reword();
    /// Shows what the current commit changes, underneath the list.
    void showCurrentChanges();

    session::RepoSession& session_;
    QString baseOid_;
    QTreeWidget* list_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* start_ = nullptr;
    QPushButton* up_ = nullptr;
    QPushButton* down_ = nullptr;
    QLabel* changesTitle_ = nullptr;
    ChangedFilesList* files_ = nullptr;
    DiffView* diff_ = nullptr;
    /// The commit whose changes are showing; answers for any other are stale.
    QString shownOid_;
    /// Full messages as loaded, to start a reword from.
    QHash<QString, QString> messages_;
};

} // namespace gity::ui
