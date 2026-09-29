#include "AccountsPanel.h"

#include "ui/panels/Confirm.h"

#include "core/git/Provider.h"
#include "session/BrowserSignIn.h"
#include "session/GitProcess.h"
#include "ui/theme/Tokens.h"

#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSettings>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace gity::ui {
namespace {

constexpr int kHostRole = Qt::UserRole + 1;
constexpr int kUserRole = Qt::UserRole + 2;

QString providerLabel(git::Provider provider) {
    const std::string_view name = git::providerName(provider);
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

/// What git is configured to remember credentials with, in the user's words.
QString credentialHelperDescription(const QString& workdir) {
    const gity::session::GitResult result = gity::session::GitProcess::run(
        workdir.isEmpty() ? QDir::homePath() : workdir,
        {QStringLiteral("config"), QStringLiteral("--get-all"),
         QStringLiteral("credential.helper")});
    const QString helper = result.output.trimmed();
    if (!result.ok() || helper.isEmpty()) {
        return AccountsPanel::tr("git has no credential helper configured, so it will ask "
                                 "every time.");
    }
    return AccountsPanel::tr("git remembers credentials with: %1")
        .arg(helper.split(QChar('\n')).join(QStringLiteral(", ")));
}

} // namespace

AccountsPanel::AccountsPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* intro = new QLabel(
        tr("Accounts let Gity tell one identity from another on the same host — a personal "
           "GitHub account and a work one, for instance. Binding a repository to an account "
           "puts that name in its remote URL, which is what makes git ask for the right "
           "credential."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    list_ = new QTreeWidget(this);
    list_->setColumnCount(3);
    list_->setHeaderLabels({tr("Service"), tr("Host"), tr("Account")});
    list_->setRootIsDecorated(false);
    list_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(list_, 1);

    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add Account…"), this);
    signIn_ = new QPushButton(tr("Sign In with Browser…"), this);
    remove_ = new QPushButton(tr("Remove"), this);
    use_ = new QPushButton(tr("Use for This Repository"), this);
    buttons->addWidget(add);
    buttons->addWidget(signIn_);
    buttons->addWidget(remove_);
    buttons->addStretch(1);
    buttons->addWidget(use_);
    layout->addLayout(buttons);

    repositoryNote_ = new QLabel(this);
    repositoryNote_->setWordWrap(true);
    applyRole(repositoryNote_, QStringLiteral("note"));
    layout->addWidget(repositoryNote_);

    // Said plainly and permanently, because the promise is the design: this
    // screen is the obvious place to expect a password field, and its absence
    // needs explaining rather than leaving as a gap.
    storageNote_ = new QLabel(this);
    storageNote_->setWordWrap(true);
    applyRole(storageNote_, QStringLiteral("note"));
    layout->addWidget(storageNote_);

    connect(add, &QPushButton::clicked, this, &AccountsPanel::addAccount);
    connect(signIn_, &QPushButton::clicked, this, &AccountsPanel::signInSelected);
    connect(remove_, &QPushButton::clicked, this, &AccountsPanel::removeSelected);
    connect(use_, &QPushButton::clicked, this, [this] {
        const session::Account account = selectedAccount();
        if (account.valid()) {
            emit useForRepositoryRequested(account.username);
        }
    });
    connect(list_, &QTreeWidget::itemSelectionChanged, this, &AccountsPanel::updateButtons);

    reload();
}

void AccountsPanel::setRepository(const QString& workdir, const QString& originUrl) {
    originUrl_ = originUrl;
    storageNote_->setText(
        tr("Gity stores no passwords or tokens. %1")
            .arg(credentialHelperDescription(workdir)));
    updateButtons();
}

void AccountsPanel::reload() {
    list_->clear();
    for (const session::Account& account : session::Accounts::load()) {
        auto* item = new QTreeWidgetItem(list_);
        item->setText(0, providerLabel(account.provider));
        item->setText(1, account.host);
        item->setText(2, account.label.isEmpty()
                             ? account.username
                             : tr("%1 (%2)").arg(account.username, account.label));
        item->setData(0, kHostRole, account.host);
        item->setData(0, kUserRole, account.username);
    }
    updateButtons();
}

session::Account AccountsPanel::selectedAccount() const {
    const QList<QTreeWidgetItem*> selected = list_->selectedItems();
    if (selected.isEmpty()) {
        return {};
    }
    session::Account account;
    account.host = selected.first()->data(0, kHostRole).toString();
    account.username = selected.first()->data(0, kUserRole).toString();
    account.provider = git::providerForHost(account.host.toStdString());
    return account;
}

void AccountsPanel::updateButtons() {
    const session::Account account = selectedAccount();
    remove_->setEnabled(account.valid());
    // Only for providers whose device grant this client knows. Enabling it
    // everywhere and failing at the request would be the disabled-button
    // mistake in reverse.
    signIn_->setEnabled(
        account.valid() &&
        git::endpointsFor(account.provider, account.host.toStdString()).valid());

    const QString remoteHost =
        QString::fromStdString(git::hostOfRemote(originUrl_.toStdString()));
    const bool sameHost =
        account.valid() && account.host.compare(remoteHost, Qt::CaseInsensitive) == 0;
    use_->setEnabled(sameHost);

    if (originUrl_.isEmpty()) {
        repositoryNote_->setText(tr("Open a repository to bind one of these to it."));
    } else if (remoteHost.isEmpty()) {
        repositoryNote_->setText(
            tr("This repository's remote is a local path, so no account applies to it."));
    } else if (account.valid() && !sameHost) {
        // Explaining the disabled button beats leaving someone to guess.
        repositoryNote_->setText(tr("That account is on %1; this repository's remote is on "
                                    "%2.")
                                     .arg(account.host, remoteHost));
    } else {
        repositoryNote_->setText(tr("This repository's remote is on %1.").arg(remoteHost));
    }
}

void AccountsPanel::addAccount() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add Account"));
    dialog.setMinimumWidth(430);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;

    auto* provider = new QComboBox(&dialog);
    for (const git::Provider candidate : git::allProviders()) {
        provider->addItem(providerLabel(candidate), static_cast<int>(candidate));
    }
    form->addRow(tr("Service"), provider);

    auto* host = new QLineEdit(&dialog);
    host->setPlaceholderText(QStringLiteral("github.com"));
    form->addRow(tr("Host"), host);

    auto* username = new QLineEdit(&dialog);
    form->addRow(tr("Account name"), username);

    auto* label = new QLineEdit(&dialog);
    label->setPlaceholderText(tr("work, personal — optional"));
    form->addRow(tr("Label"), label);
    layout->addLayout(form);

    auto* note = new QLabel(
        tr("No password is asked for here. The next time git needs one for this host it will "
           "prompt, and your credential helper remembers it from there."),
        &dialog);
    note->setWordWrap(true);
    applyRole(note, QStringLiteral("note"));
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    // The host follows the service until the user edits it themselves.
    bool hostEdited = false;
    connect(host, &QLineEdit::textEdited, &dialog, [&hostEdited] { hostEdited = true; });
    const auto syncHost = [&] {
        if (hostEdited) {
            return;
        }
        const auto chosen = static_cast<git::Provider>(provider->currentData().toInt());
        const std::string_view suggested = git::defaultHost(chosen);
        host->setText(QString::fromUtf8(suggested.data(),
                                        static_cast<qsizetype>(suggested.size())));
    };
    connect(provider, &QComboBox::currentIndexChanged, &dialog, syncHost);
    syncHost();

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    session::Account account;
    account.host = host->text().trimmed();
    account.username = username->text().trimmed();
    account.label = label->text().trimmed();
    account.provider = static_cast<git::Provider>(provider->currentData().toInt());
    if (!account.valid()) {
        return;
    }
    // GitHub sign-in names are never emails, and git sends this name to the
    // credential helper as it is: an email here gets no token, and a URL made
    // from it is the one that broke a submodule. Asked, not refused — a
    // self-hosted service may use emails.
    if (account.provider == git::Provider::GitHub && account.username.contains(QChar('@'))) {
        const auto answer = QMessageBox::question(
            this, tr("Use an email as the account name?"),
            tr("GitHub account names are never email addresses — use the login shown on your "
               "GitHub profile (for GitHub CLI, the name in gh auth status). Git sends this name "
               "to find your token, so an email finds nothing.\n\nKeep \u201c%1\u201d anyway?")
                .arg(account.username),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    session::Accounts::add(account);
    reload();
}

void AccountsPanel::signInSelected() {
    const session::Account account = selectedAccount();
    if (!account.valid()) {
        return;
    }

    // The client ID belongs to an OAuth application registered by whoever runs
    // this — there is no shared secret to embed, and borrowing another
    // product's ID would put Gity's traffic under their name. Asked for once
    // and remembered per host.
    QSettings store(QStringLiteral("Gity"), QStringLiteral("Gity"));
    const QString key = QStringLiteral("oauth/clientId/") + account.host;
    QString clientId = store.value(key).toString();
    if (clientId.isEmpty()) {
        bool accepted = false;
        clientId = QInputDialog::getText(
                       this, tr("Client ID for %1").arg(account.host),
                       tr("Signing in through a browser needs an OAuth application "
                          "registered on %1, and its client ID.\n\n"
                          "On GitHub: Settings ▸ Developer settings ▸ OAuth Apps ▸ New OAuth "
                          "App, then enable device flow. No client secret is needed.")
                           .arg(account.host),
                       QLineEdit::Normal, QString(), &accepted)
                       .trimmed();
        if (!accepted || clientId.isEmpty()) {
            return;
        }
        store.setValue(key, clientId);
    }

    auto* dialog = new QProgressDialog(tr("Contacting %1…").arg(account.host), tr("Cancel"), 0,
                                       0, this);
    dialog->setWindowTitle(tr("Sign In"));
    dialog->setMinimumWidth(430);
    dialog->setAutoClose(false);
    dialog->setAutoReset(false);

    auto* signIn = new session::BrowserSignIn(this);
    connect(signIn, &session::BrowserSignIn::codeReady, dialog,
            [dialog](const QString& code, const QString& url) {
                // The code is shown as well as opened, because the browser may
                // land somewhere the user cannot read it back from.
                dialog->setLabelText(
                    tr("Enter this code in your browser:\n\n%1\n\nIf the page did not "
                       "open, go to %2")
                        .arg(code, url));
            });
    connect(signIn, &session::BrowserSignIn::finished, dialog,
            [this, dialog, signIn](bool ok, const QString& message) {
                dialog->close();
                dialog->deleteLater();
                signIn->deleteLater();
                if (ok) {
                    QMessageBox::information(this, tr("Signed in"), message);
                } else {
                    QMessageBox::warning(this, tr("Sign-in failed"), message);
                }
            });
    // Cancel ends it: the flow stops listening, and both objects go rather
    // than lingering for the life of the panel.
    connect(dialog, &QProgressDialog::canceled, signIn, [dialog, signIn] {
        signIn->cancel();
        dialog->deleteLater();
        signIn->deleteLater();
    });

    dialog->show();
    signIn->start(account.provider, account.host, account.username, clientId);
}

void AccountsPanel::removeSelected() {
    const session::Account account = selectedAccount();
    if (!account.valid()) {
        return;
    }
    // No confirmation: this forgets a name, not a credential. The token stays
    // exactly where it was, in the helper, and adding the account back costs
    // one dialog.
    session::Accounts::remove(account);
    reload();
}

} // namespace gity::ui
