#include "SettingsDialog.h"

#include "ui/panels/Confirm.h"

#include "session/GitProcess.h"
#include "ui/panels/AccountsPanel.h"
#include "ui/panels/CredentialsPanel.h"
#include "ui/theme/Theme.h"
#include "ui/theme/Tokens.h"

#include <git2.h>

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace gity::ui {
namespace {

using gity::ui::Theme;

/// A section that states facts rather than offering settings. Worth having:
/// most of what people want from a settings screen the first time is to find
/// out what the tool is already doing.
QWidget* buildAboutSection(QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* form = new QFormLayout;
    form->addRow(SettingsDialog::tr("git"),
                 new QLabel(gity::session::GitProcess::version().isEmpty()
                                ? SettingsDialog::tr("not found")
                                : gity::session::GitProcess::version(),
                            page));
    form->addRow(SettingsDialog::tr("git path"),
                 new QLabel(gity::session::GitProcess::executable(), page));
    // The linked library, not the header macro: they can differ, and the one
    // that matters is the code actually running.
    int major = 0;
    int minor = 0;
    int patch = 0;
    git_libgit2_version(&major, &minor, &patch);
    form->addRow(SettingsDialog::tr("libgit2"),
                 new QLabel(QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch), page));
    form->addRow(SettingsDialog::tr("Qt"), new QLabel(QStringLiteral(QT_VERSION_STR), page));

    layout->addLayout(form);

    auto* note = new QLabel(
        SettingsDialog::tr("Gity runs the git you already have rather than shipping its own, "
                           "so hooks, filters, LFS and your credential helper all behave "
                           "exactly as they do in a terminal."),
        page);
    note->setWordWrap(true);
    applyRole(note, QStringLiteral("note"));
    layout->addWidget(note);
    layout->addStretch(1);
    return page;
}

/// Appearance. Live, because choosing a theme from a preview you cannot see
/// is choosing blind — the dialog stays open and the window behind it changes.
QWidget* buildAppearanceSection(QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* form = new QFormLayout;
    auto* theme = new QComboBox(page);
    for (const Theme::Variant option : Theme::choices()) {
        theme->addItem(Theme::displayName(option), static_cast<int>(option));
    }
    theme->setItemData(0, SettingsDialog::tr("Navy Dark or Navy Light, as the desktop is set."),
                       Qt::ToolTipRole);
    theme->setCurrentIndex(theme->findData(static_cast<int>(Theme::variant())));
    form->addRow(SettingsDialog::tr("Theme"), theme);
    auto* density = new QComboBox(page);
    density->addItem(SettingsDialog::tr("Comfortable"), false);
    density->addItem(SettingsDialog::tr("Compact"), true);
    density->setCurrentIndex(Theme::compact() ? 1 : 0);
    form->addRow(SettingsDialog::tr("History rows"), density);
    QObject::connect(density, &QComboBox::currentIndexChanged, page, [density](int) {
        Theme::setCompact(density->currentData().toBool(),
                          qobject_cast<QApplication*>(QCoreApplication::instance()));
    });
    layout->addLayout(form);

    QObject::connect(theme, &QComboBox::currentIndexChanged, page, [theme](int) {
        Theme::setVariant(static_cast<Theme::Variant>(theme->currentData().toInt()),
                          qobject_cast<QApplication*>(QCoreApplication::instance()));
    });

    auto* note = new QLabel(
        SettingsDialog::tr("Applied immediately and remembered."),
        page);
    note->setWordWrap(true);
    applyRole(note, QStringLiteral("note"));
    layout->addWidget(note);
    layout->addStretch(1);
    return page;
}

} // namespace

SettingsDialog::SettingsDialog(const QString& workdir, const QString& originUrl,
                               const QList<QPair<QString, QString>>& remotes, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    setModal(true);
    resize(760, 480);

    auto* layout = new QVBoxLayout(this);
    auto* columns = new QHBoxLayout;

    sections_ = new QListWidget(this);
    sections_->setFixedWidth(160);
    sections_->setObjectName(QStringLiteral("settingsSections"));
    columns->addWidget(sections_);

    pages_ = new QStackedWidget(this);
    columns->addWidget(pages_, 1);
    layout->addLayout(columns, 1);

    accounts_ = new AccountsPanel(this);
    accounts_->setRepository(workdir, originUrl);
    connect(accounts_, &AccountsPanel::useForRepositoryRequested, this,
            [this](const QString& username) {
                emit useAccountForRepository(username);
                accept();
            });
    addSection(tr("Accounts"), accounts_);
    accountsIndex_ = pages_->count() - 1;

    // Built only when opened: it asks the credential helper on creation, and
    // a keychain prompt is not something opening Settings should cause.
    auto* credentials = new CredentialsPanel(this);
    credentials_ = credentials;
    connect(credentials, &CredentialsPanel::repositoryAccountChosen, this,
            &SettingsDialog::repositoryAccountChosen);
    addSection(tr("Credentials"), credentials);
    credentialsIndex_ = pages_->count() - 1;
    connect(sections_, &QListWidget::currentRowChanged, this,
            [this, credentials, workdir, remotes, loaded = false](int row) mutable {
                if (row == credentialsIndex_ && !loaded) {
                    loaded = true;
                    credentials->setRepository(workdir, remotes);
                }
            });

    addSection(tr("Appearance"), buildAppearanceSection(this));
    appearanceIndex_ = pages_->count() - 1;
    addSection(tr("About"), buildAboutSection(this));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);

    connect(sections_, &QListWidget::currentRowChanged, pages_,
            &QStackedWidget::setCurrentIndex);
    sections_->setCurrentRow(0);
}

void SettingsDialog::addSection(const QString& name, QWidget* page) {
    sections_->addItem(name);
    auto* wrapper = new QWidget(this);
    auto* layout = new QVBoxLayout(wrapper);
    layout->setContentsMargins(14, 12, 4, 4);
    layout->addWidget(page);
    pages_->addWidget(wrapper);
}

void SettingsDialog::setRepositoryAccount(const QString& host, bool viaGitHubCli,
                                          const QStringList& accounts,
                                          const QString& defaultAccount, const QString& chosen) {
    credentials_->setRepositoryAccount(host, viaGitHubCli, accounts, defaultAccount, chosen);
}

void SettingsDialog::showCredentials() {
    if (credentialsIndex_ >= 0) {
        sections_->setCurrentRow(credentialsIndex_);
    }
}

void SettingsDialog::showAppearance() {
    if (appearanceIndex_ >= 0) {
        sections_->setCurrentRow(appearanceIndex_);
    }
}

void SettingsDialog::showAccounts() {
    if (accountsIndex_ >= 0) {
        sections_->setCurrentRow(accountsIndex_);
    }
}

} // namespace gity::ui
