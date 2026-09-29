// Repository ▸ Remotes.
//
// A table rather than a list: a remote is a name and a URL, and the URL is the
// part people actually check when something pushes somewhere unexpected. A
// push URL that differs from the fetch URL gets its own line, because that
// difference is precisely the thing worth noticing.
#pragma once

#include "session/RepoSession.h"

#include <QDialog>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace gity::ui {

class RemotesDialog : public QDialog {
    Q_OBJECT

public:
    explicit RemotesDialog(session::RepoSession& session, QWidget* parent = nullptr);

    /// True when anything was added, renamed, removed or repointed, so the
    /// window knows whether reopening the repository is warranted.
    [[nodiscard]] bool changed() const noexcept { return changed_; }

private:
    void setRemotes(session::RemoteListPtr remotes);
    void addRemote();
    void renameSelected();
    void removeSelected();
    void editUrlOfSelected();
    void updateButtons();

    /// The selected remote's name, or empty when nothing is selected.
    [[nodiscard]] QString selectedName() const;

    /// Asks for a name, re-asking while `checkRemoteName` refuses it. Returns
    /// empty if the user cancels.
    [[nodiscard]] QString askForName(const QString& title, const QString& initial);

    session::RepoSession& session_;
    session::RemoteListPtr remotes_;
    QTreeWidget* tree_ = nullptr;
    QPushButton* rename_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* editUrl_ = nullptr;
    QLabel* error_ = nullptr;
    bool changed_ = false;
};

} // namespace gity::ui
