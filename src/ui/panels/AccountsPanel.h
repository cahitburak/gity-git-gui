// Settings ▸ Accounts.
//
// Shows which accounts Gity knows about and lets a repository be bound to one.
// It deliberately does **not** show tokens, offer to store them, or claim to
// know whether one is valid — because it holds none. ADR-010: the credential
// lives in git's helper, and a screen that implied otherwise would be the
// first step towards this client keeping a second copy of a password.
//
// The panel's real work is the case that keeps biting: a personal account and
// a work account on the same host, where git picks whichever the helper
// answers with and the wrong one produces "repository not found".
#pragma once

#include "session/Accounts.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace gity::ui {

class AccountsPanel : public QWidget {
    Q_OBJECT

public:
    explicit AccountsPanel(QWidget* parent = nullptr);

    /// The repository the "use for this repository" action applies to, and its
    /// origin URL. Empty disables that action rather than hiding it, so the
    /// screen reads the same whether or not a repository is open.
    void setRepository(const QString& workdir, const QString& originUrl);

signals:
    /// The user bound the open repository to `username`; the host should
    /// rewrite origin's URL.
    void useForRepositoryRequested(const QString& username);

private:
    void reload();
    void addAccount();
    void signInSelected();
    void removeSelected();
    void updateButtons();
    [[nodiscard]] session::Account selectedAccount() const;

    QTreeWidget* list_ = nullptr;
    QLabel* storageNote_ = nullptr;
    QLabel* repositoryNote_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* use_ = nullptr;
    QPushButton* signIn_ = nullptr;
    QString originUrl_;
};

} // namespace gity::ui
