#include "CredentialsPanel.h"

#include "session/CredentialInventory.h"
#include "session/CredentialManager.h"
#include "core/git/RepoAccount.h"
#include "ui/panels/Confirm.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>
#include <QHeaderView>
#include <QSignalBlocker>

#include <algorithm>
#include <QInputDialog>
#include <QMessageBox>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace gity::ui {
namespace {

/// The address with any password in it blanked, for showing. A URL can carry
/// `user:secret@`, and this page must not print a secret it was not asked for.
QString displayUrl(const QString& url) {
    const QUrl parsed(url);
    if (parsed.isValid() && !parsed.password().isEmpty()) {
        return parsed.toDisplayString(QUrl::RemovePassword);
    }
    return url;
}

} // namespace

CredentialsPanel::CredentialsPanel(QWidget* parent) : QWidget(parent) {
    manager_ = new session::CredentialManager(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // This repository's account first: it is the question people come here
    // with. Set in the repository's own config — and its submodules' — so it
    // applies in a terminal too and never changes another repository.
    auto* repoTitle = new QLabel(tr("This repository"), this);
    QFont repoTitleFont = repoTitle->font();
    repoTitleFont.setBold(true);
    repoTitle->setFont(repoTitleFont);
    layout->addWidget(repoTitle);
    auto* repoRow = new QHBoxLayout;
    repoAccountLabel_ = new QLabel(this);
    repoAccount_ = new QComboBox(this);
    repoAccount_->setMinimumContentsLength(24);
    repoAccountApply_ = new QPushButton(tr("Apply"), this);
    applyRole(repoAccountApply_, QStringLiteral("primary"));
    repoRow->addWidget(repoAccountLabel_);
    repoRow->addWidget(repoAccount_, 1);
    repoRow->addWidget(repoAccountApply_);
    layout->addLayout(repoRow);
    repoAccountNote_ = new QLabel(this);
    repoAccountNote_->setWordWrap(true);
    applyRole(repoAccountNote_, QStringLiteral("note"));
    layout->addWidget(repoAccountNote_);
    connect(repoAccount_, &QComboBox::currentIndexChanged, this, [this](int) {
        repoAccountApply_->setEnabled(repoAccount_->currentData().toString() != repoAccountChosen_);
    });
    connect(repoAccountApply_, &QPushButton::clicked, this, [this] {
        repoAccountChosen_ = repoAccount_->currentData().toString();
        repoAccountApply_->setEnabled(false);
        emit repositoryAccountChosen(repoAccountChosen_);
    });
    setRepositoryAccount({}, false, {}, {}, {});
    layout->addSpacing(12);

    auto* intro = new QLabel(
        tr("The credential git uses for a remote, as your credential helper holds it. Gity keeps "
           "no copy: this page asks the helper, and saving or removing changes what the helper "
           "stores — for git in a terminal too."),
        this);
    intro->setWordWrap(true);
    applyRole(intro, QStringLiteral("note"));
    layout->addWidget(intro);

    helpers_ = new QLabel(this);
    helpers_->setWordWrap(true);
    helpers_->setTextFormat(Qt::PlainText);
    helpers_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(helpers_);

    auto* form = new QFormLayout;
    remote_ = new QComboBox(this);
    form->addRow(tr("Remote"), remote_);
    otherUrl_ = new QLineEdit(this);
    otherUrl_->setPlaceholderText(QStringLiteral("https://host/owner/repository.git"));
    otherUrl_->setVisible(false);
    form->addRow(QString(), otherUrl_);

    username_ = new QLineEdit(this);
    form->addRow(tr("Username"), username_);

    password_ = new QLineEdit(this);
    password_->setEchoMode(QLineEdit::Password);
    reveal_ = new QPushButton(tr("Show"), this);
    reveal_->setCheckable(true);
    auto* passwordRow = new QHBoxLayout;
    passwordRow->addWidget(password_, 1);
    passwordRow->addWidget(reveal_);
    form->addRow(tr("Password or token"), passwordRow);
    layout->addLayout(form);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    applyRole(status_, QStringLiteral("muted"));
    layout->addWidget(status_);

    auto* buttons = new QHBoxLayout;
    refresh_ = new QPushButton(tr("Look Up Again"), this);
    remove_ = new QPushButton(tr("Remove"), this);
    save_ = new QPushButton(tr("Save"), this);
    applyRole(remove_, QStringLiteral("destructive"));
    applyRole(save_, QStringLiteral("primary"));
    buttons->addWidget(refresh_);
    buttons->addStretch(1);
    buttons->addWidget(remove_);
    buttons->addWidget(save_);
    layout->addLayout(buttons);

    // Everything stored on this computer, from every store that can be listed
    // — git itself cannot list, so each store is read directly
    // (CredentialInventory). Secrets are fetched only when shown.
    auto* storedTitle = new QLabel(tr("Stored on this computer"), this);
    QFont titleFont = storedTitle->font();
    titleFont.setBold(true);
    storedTitle->setFont(titleFont);
    layout->addSpacing(10);
    layout->addWidget(storedTitle);

    stored_ = new QTreeWidget(this);
    stored_->setColumnCount(3);
    stored_->setHeaderLabels({tr("Store"), tr("Host"), tr("Account")});
    stored_->setRootIsDecorated(false);
    stored_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    stored_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    stored_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    stored_->setMinimumHeight(120);
    layout->addWidget(stored_, 1);

    storedNote_ = new QLabel(this);
    storedNote_->setWordWrap(true);
    applyRole(storedNote_, QStringLiteral("note"));
    layout->addWidget(storedNote_);

    auto* storedButtons = new QHBoxLayout;
    auto* storedRefresh = new QPushButton(tr("Refresh"), this);
    storedShow_ = new QPushButton(tr("Show…"), this);
    storedUse_ = new QPushButton(tr("Use for git"), this);
    storedUse_->setToolTip(tr("Make this the GitHub CLI account git gets for its host "
                              "(gh auth switch)."));
    storedRemove_ = new QPushButton(tr("Remove…"), this);
    applyRole(storedRemove_, QStringLiteral("destructive"));
    storedButtons->addWidget(storedRefresh);
    storedButtons->addStretch(1);
    storedButtons->addWidget(storedShow_);
    storedButtons->addWidget(storedUse_);
    storedButtons->addWidget(storedRemove_);
    layout->addLayout(storedButtons);

    inventory_ = new session::CredentialInventory(this);
    connect(storedRefresh, &QPushButton::clicked, this, &CredentialsPanel::refreshStored);
    connect(stored_, &QTreeWidget::itemSelectionChanged, this,
            &CredentialsPanel::updateStoredButtons);
    connect(inventory_, &session::CredentialInventory::listed, this,
            &CredentialsPanel::showStored);
    connect(storedShow_, &QPushButton::clicked, this, [this] {
        const int index = selectedStored();
        if (index >= 0) {
            storedNote_->setText(tr("Asking the store…"));
            inventory_->reveal(storedList_.at(index));
        }
    });
    connect(inventory_, &session::CredentialInventory::revealed, this,
            [this](const QString& secret, const QString& problem) {
                storedNote_->clear();
                if (secret.isEmpty()) {
                    QMessageBox::information(this, tr("Credential"),
                                             problem.isEmpty() ? tr("Nothing is stored.") : problem);
                    return;
                }
                // Shown in a selectable field, so it can be copied, and gone
                // with the dialog.
                QInputDialog::getText(this, tr("Credential"),
                                      tr("The password or token. It stays in its store; "
                                         "Gity keeps no copy."),
                                      QLineEdit::Normal, secret);
            });
    connect(storedUse_, &QPushButton::clicked, this, [this] {
        const int index = selectedStored();
        if (index >= 0) {
            inventory_->makeActive(storedList_.at(index));
        }
    });
    connect(storedRemove_, &QPushButton::clicked, this, [this] {
        const int index = selectedStored();
        if (index < 0) {
            return;
        }
        const session::StoredCredential& credential = storedList_.at(index);
        const bool gh = credential.source == session::StoredCredential::Source::GitHubCli;
        if (!confirmDestructive(
                this, tr("Remove credential?"),
                gh ? tr("Sign %1 out of GitHub CLI on %2?").arg(credential.username, credential.host)
                   : tr("Remove the credential for %1 on %2?")
                         .arg(credential.username, credential.host),
                tr("git will ask for it again the next time it needs it — in a terminal as well "
                   "as in Gity."),
                gh ? tr("Sign Out") : tr("Remove"))) {
            return;
        }
        inventory_->remove(credential);
    });
    connect(inventory_, &session::CredentialInventory::finished, this,
            [this](bool, const QString& message) {
                storedNote_->setText(message);
                refreshStored();
                lookUp(); // the per-remote answer above may have changed too
            });
    updateStoredButtons();

    connect(reveal_, &QPushButton::toggled, this, [this](bool shown) {
        password_->setEchoMode(shown ? QLineEdit::Normal : QLineEdit::Password);
        reveal_->setText(shown ? tr("Hide") : tr("Show"));
    });
    connect(remote_, &QComboBox::currentIndexChanged, this, [this](int) {
        otherUrl_->setVisible(remote_->currentData().toString().isEmpty());
        lookUp();
    });
    connect(otherUrl_, &QLineEdit::editingFinished, this, &CredentialsPanel::lookUp);
    connect(refresh_, &QPushButton::clicked, this, &CredentialsPanel::lookUp);
    connect(save_, &QPushButton::clicked, this, &CredentialsPanel::save);
    connect(remove_, &QPushButton::clicked, this, &CredentialsPanel::removeStored);

    connect(manager_, &session::CredentialManager::lookedUp, this,
            [this](const QString& url, bool found, const QString& username,
                   const QString& password, const QString& problem) {
                if (url != currentUrl()) {
                    return; // the selection moved on while this was answering
                }
                found_ = found;
                storedUsername_ = username;
                storedPassword_ = password;
                if (found || !username.isEmpty()) {
                    username_->setText(username);
                }
                password_->setText(password);
                setBusy(false, found ? tr("Stored for this remote.")
                               : problem.isEmpty()
                                   ? tr("Nothing is stored for this remote. Enter a username and "
                                        "token and save, and git will use them from now on.")
                                   : problem);
            });
    connect(manager_, &session::CredentialManager::finished, this,
            [this](bool ok, const QString& message) {
                setBusy(false, message);
                if (ok) {
                    lookUp(); // show what the helper now actually holds
                }
            });
}

void CredentialsPanel::setRepository(const QString& workdir,
                                     const QList<QPair<QString, QString>>& remotes) {
    workdir_ = workdir;

    const QStringList helpers = session::CredentialManager::configuredHelpers(workdir);
    if (helpers.isEmpty()) {
        helpers_->setText(tr("No credential helper is configured, so git asks for a password "
                             "every time and nothing can be stored or shown here."));
        applyRole(helpers_, QStringLiteral("warn"));
    } else {
        // Gity's own per-repository helper is a shell one-liner; said in words.
        QStringList shown;
        for (const QString& line : helpers) {
            const QString account =
                QString::fromStdString(git::accountOfGhHelper(line.toStdString()));
            shown << (account.isEmpty()
                          ? line
                          : tr("%1  \u2192 GitHub CLI token for %2, this repository only")
                                .arg(line.section(QChar(' '), 0, 1).section(QChar('\t'), 0, 1),
                                     account));
        }
        helpers_->setText(tr("Helpers:\n%1").arg(shown.join(QChar('\n'))));
        applyRole(helpers_, QStringLiteral("muted"));
    }

    const QSignalBlocker quiet(remote_);
    remote_->clear();
    for (const auto& [name, url] : remotes) {
        remote_->addItem(QStringLiteral("%1 — %2").arg(name, displayUrl(url)), url);
    }
    // Empty data marks the free-form entry.
    remote_->addItem(tr("Other URL…"), QString());
    otherUrl_->setVisible(remotes.isEmpty());
    lookUp();
    refreshStored();
}

void CredentialsPanel::setRepositoryAccount(const QString& host, bool viaGitHubCli,
                                            const QStringList& accounts,
                                            const QString& defaultAccount,
                                            const QString& chosen) {
    repoAccountChosen_ = chosen;
    const QSignalBlocker quiet(repoAccount_);
    repoAccount_->clear();
    const bool choosable = !host.isEmpty();
    repoAccountLabel_->setText(choosable ? tr("Account for %1").arg(host) : QString());
    repoAccount_->setVisible(choosable);
    repoAccountApply_->setVisible(choosable);
    repoAccountLabel_->setVisible(choosable);
    if (!choosable) {
        repoAccountNote_->setText(tr("This repository's remote is not an https address, so the "
                                     "account is decided by its SSH key or path — there is "
                                     "nothing to choose here."));
        return;
    }
    repoAccount_->addItem(defaultAccount.isEmpty()
                              ? tr("Default — whatever git's helper gives")
                              : tr("Default — %1 (%2)").arg(defaultAccount,
                                                            viaGitHubCli ? tr("GitHub CLI's active "
                                                                              "account")
                                                                         : tr("helper default")),
                          QString());
    for (const QString& account : accounts) {
        repoAccount_->addItem(account, account);
    }
    if (!chosen.isEmpty() && repoAccount_->findData(chosen) < 0) {
        repoAccount_->addItem(tr("%1 (not signed in)").arg(chosen), chosen);
    }
    repoAccount_->setCurrentIndex(std::max(0, repoAccount_->findData(chosen)));
    repoAccountApply_->setEnabled(false);
    repoAccountNote_->setText(
        viaGitHubCli
            ? tr("Applies to this repository and its submodules on %1, in their own git config, "
                 "so a terminal uses it too; other repositories are not touched. GitHub CLI "
                 "keeps the tokens — the chosen account need not be its active one.")
                  .arg(host)
            : tr("Applies to this repository and its submodules on %1, in their own git config: "
                 "git asks your credential helper for this account's credential.")
                  .arg(host));
}

void CredentialsPanel::refreshStored() {
    stored_->clear();
    storedList_.clear();
    storedNote_->setText(tr("Reading the stores…"));
    updateStoredButtons();
    inventory_->list();
}

void CredentialsPanel::showStored(const QList<session::StoredCredential>& credentials,
                                  const QStringList& notes) {
    using Source = session::StoredCredential::Source;
    storedList_ = credentials;
    stored_->clear();
    for (int i = 0; i < credentials.size(); ++i) {
        const session::StoredCredential& credential = credentials.at(i);
        auto* item = new QTreeWidgetItem(stored_);
        item->setText(0, credential.source == Source::Keyring ? tr("Keyring")
                         : credential.source == Source::File  ? tr("Plain-text file")
                                                              : tr("GitHub CLI"));
        item->setText(1, credential.path.isEmpty()
                             ? credential.host
                             : QStringLiteral("%1/%2").arg(credential.host, credential.path));
        item->setText(2, credential.active ? tr("%1 — in use").arg(credential.username)
                                           : credential.username);
        item->setToolTip(0, credential.location);
        item->setData(0, Qt::UserRole, i);
    }
    QString note = credentials.isEmpty()
                       ? tr("No credentials are stored where Gity can list them.")
                       : tr("GitHub CLI gives git the account in use, unless a repository has "
                            "its own assigned above. Keyring: git-credential-libsecret. macOS "
                            "Keychain and Windows Credential Manager are not listed yet; use the "
                            "lookup above for those.");
    if (!notes.isEmpty()) {
        note += QChar(' ') + tr("Not read: %1.").arg(notes.join(QStringLiteral("; ")));
    }
    storedNote_->setText(note);
    updateStoredButtons();
}

int CredentialsPanel::selectedStored() const {
    const QList<QTreeWidgetItem*> selected = stored_->selectedItems();
    return selected.isEmpty() ? -1 : selected.first()->data(0, Qt::UserRole).toInt();
}

void CredentialsPanel::updateStoredButtons() {
    const int index = selectedStored();
    const bool any = index >= 0 && index < storedList_.size();
    storedShow_->setEnabled(any);
    storedRemove_->setEnabled(any);
    storedUse_->setEnabled(any &&
                           storedList_.at(index).source ==
                               session::StoredCredential::Source::GitHubCli &&
                           !storedList_.at(index).active);
}

QString CredentialsPanel::currentUrl() const {
    const QString url = remote_->currentData().toString();
    return url.isEmpty() ? otherUrl_->text().trimmed() : url;
}

void CredentialsPanel::setBusy(bool busy, const QString& status) {
    status_->setText(status);
    const bool usable = session::CredentialManager::usesCredentials(currentUrl());
    for (QWidget* control : {static_cast<QWidget*>(username_), static_cast<QWidget*>(password_),
                             static_cast<QWidget*>(reveal_), static_cast<QWidget*>(save_)}) {
        control->setEnabled(!busy && usable);
    }
    remove_->setEnabled(!busy && usable && found_);
    refresh_->setEnabled(!busy && usable);
}

void CredentialsPanel::lookUp() {
    // Never carry a revealed secret from one remote to the next.
    reveal_->setChecked(false);
    found_ = false;
    storedUsername_.clear();
    storedPassword_.clear();
    username_->clear();
    password_->clear();

    const QString url = currentUrl();
    if (url.isEmpty()) {
        setBusy(false, tr("Enter the remote's address."));
        return;
    }
    if (!session::CredentialManager::usesCredentials(url)) {
        setBusy(false, tr("This remote authenticates with SSH keys or not at all, so there is "
                          "no stored credential to show."));
        return;
    }
    setBusy(true, tr("Asking your credential helper…"));
    manager_->lookup(workdir_, url);
}

void CredentialsPanel::save() {
    const QString username = username_->text().trimmed();
    const QString password = password_->text();
    if (username.isEmpty() || password.isEmpty()) {
        setBusy(false, tr("Enter both a username and a password or token."));
        return;
    }
    setBusy(true, tr("Saving…"));
    manager_->store(workdir_, currentUrl(), username, password,
                    found_ ? storedUsername_ : QString(), found_ ? storedPassword_ : QString());
}

void CredentialsPanel::removeStored() {
    if (!found_) {
        return;
    }
    if (!confirmDestructive(this, tr("Remove credential?"),
                            tr("Remove the credential for %1 from your credential helper?")
                                .arg(storedUsername_),
                            tr("git will ask for it again next time it talks to this remote — "
                               "from a terminal as well as from Gity."),
                            tr("Remove"))) {
        return;
    }
    setBusy(true, tr("Removing…"));
    manager_->remove(workdir_, currentUrl(), storedUsername_, storedPassword_);
}

} // namespace gity::ui
