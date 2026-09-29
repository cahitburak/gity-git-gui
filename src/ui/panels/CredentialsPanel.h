// Settings ▸ Credentials — the credential git is using for each remote.
//
// Everything shown here is read from, and written to, the user's own
// credential helper through `git credential`; nothing is kept by Gity
// (ADR-010). The secret is masked until asked for, and lives only as long as
// the page does.
#pragma once

#include <QList>
#include <QPair>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

#include "session/CredentialInventory.h"

namespace gity::session {
class CredentialManager;
}

class QTreeWidget;

namespace gity::ui {

class CredentialsPanel : public QWidget {
    Q_OBJECT

public:
    explicit CredentialsPanel(QWidget* parent = nullptr);

    /// The repository whose remotes are offered, as (name, url) pairs.
    void setRepository(const QString& workdir, const QList<QPair<QString, QString>>& remotes);

    /// The account this repository uses for its remote's host, and the ones
    /// it could use instead. `host` empty: the remote is not https, so there
    /// is nothing to choose here.
    void setRepositoryAccount(const QString& host, bool viaGitHubCli, const QStringList& accounts,
                              const QString& defaultAccount, const QString& chosen);

signals:
    /// The user assigned `account` to this repository; empty means default.
    void repositoryAccountChosen(const QString& account);

private:
    [[nodiscard]] QString currentUrl() const;
    void lookUp();
    void save();
    void removeStored();
    void setBusy(bool busy, const QString& status);
    void refreshStored();
    void showStored(const QList<session::StoredCredential>& credentials, const QStringList& notes);
    void updateStoredButtons();
    [[nodiscard]] int selectedStored() const;

    session::CredentialManager* manager_ = nullptr;
    session::CredentialInventory* inventory_ = nullptr;
    QTreeWidget* stored_ = nullptr;
    QLabel* storedNote_ = nullptr;
    QPushButton* storedShow_ = nullptr;
    QPushButton* storedRemove_ = nullptr;
    QPushButton* storedUse_ = nullptr;
    QList<session::StoredCredential> storedList_;
    QString workdir_;
    QLabel* helpers_ = nullptr;
    QLabel* repoAccountLabel_ = nullptr;
    QComboBox* repoAccount_ = nullptr;
    QPushButton* repoAccountApply_ = nullptr;
    QLabel* repoAccountNote_ = nullptr;
    QString repoAccountChosen_;
    QComboBox* remote_ = nullptr;
    QLineEdit* otherUrl_ = nullptr;
    QLabel* status_ = nullptr;
    QLineEdit* username_ = nullptr;
    QLineEdit* password_ = nullptr;
    QPushButton* reveal_ = nullptr;
    QPushButton* refresh_ = nullptr;
    QPushButton* save_ = nullptr;
    QPushButton* remove_ = nullptr;

    /// What the helper answered, so a changed username can erase the old
    /// entry rather than leave both behind.
    QString storedUsername_;
    QString storedPassword_;
    bool found_ = false;
};

} // namespace gity::ui
