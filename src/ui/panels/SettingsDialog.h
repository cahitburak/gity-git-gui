// Settings.
//
// A sidebar of sections rather than tabs, because the list will grow and tabs
// stop being readable somewhere around six. Every section is a widget that
// knows nothing about this dialog, so a section can be shown anywhere else
// without being untangled from it first.
#pragma once

#include <QDialog>
#include <QList>
#include <QPair>

class QListWidget;
class QStackedWidget;

namespace gity::ui {

class AccountsPanel;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    /// `workdir` and `originUrl` describe the repository currently open, which
    /// the Accounts section can bind to an account. Both may be empty.
    /// `remotes` are the repository's (name, url) pairs, for the Credentials
    /// section.
    SettingsDialog(const QString& workdir, const QString& originUrl,
                   const QList<QPair<QString, QString>>& remotes, QWidget* parent = nullptr);

    /// Opens on the Accounts section, for callers that got here from a
    /// credential problem rather than from the menu.
    void showAccounts();
    void showAppearance();
    /// Opens on the Credentials section.
    void showCredentials();
    /// Forwarded to the Credentials section; see CredentialsPanel.
    void setRepositoryAccount(const QString& host, bool viaGitHubCli, const QStringList& accounts,
                              const QString& defaultAccount, const QString& chosen);

signals:
    void useAccountForRepository(const QString& username);
    void repositoryAccountChosen(const QString& account);

private:
    void addSection(const QString& name, QWidget* page);

    QListWidget* sections_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    AccountsPanel* accounts_ = nullptr;
    int accountsIndex_ = -1;
    int credentialsIndex_ = -1;
    int appearanceIndex_ = -1;
    class CredentialsPanel* credentials_ = nullptr;
};

} // namespace gity::ui
