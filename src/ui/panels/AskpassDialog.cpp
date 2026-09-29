#include "AskpassDialog.h"

#include "ui/panels/Confirm.h"

#include "ui/theme/Tokens.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace gity::ui {
namespace {

QString titleFor(git::AskpassKind kind) {
    switch (kind) {
    case git::AskpassKind::Username:
        return QCoreApplication::translate("askpass", "Username");
    case git::AskpassKind::Password:
        return QCoreApplication::translate("askpass", "Password");
    case git::AskpassKind::Passphrase:
        return QCoreApplication::translate("askpass", "SSH Key Passphrase");
    case git::AskpassKind::HostKey:
        return QCoreApplication::translate("askpass", "Unrecognised Host");
    case git::AskpassKind::Unknown:
        break;
    }
    return QCoreApplication::translate("askpass", "Git Credential");
}

/// What is worth saying, per kind. Empty where there is nothing honest to add.
QString noteFor(git::AskpassKind kind) {
    switch (kind) {
    case git::AskpassKind::Password:
    case git::AskpassKind::Unknown:
        return QCoreApplication::translate(
            "askpass", "Gity does not store this. Whether it is remembered is decided by the "
                       "credential helper git is already configured to use.");
    case git::AskpassKind::Passphrase:
        return QCoreApplication::translate(
            "askpass", "This unlocks a key on this machine. Gity does not store it.");
    case git::AskpassKind::HostKey:
        return QCoreApplication::translate(
            "askpass", "Only answer yes if you recognise this fingerprint. An unexpected one "
                       "means something is between you and the server.");
    case git::AskpassKind::Username:
        break;
    }
    return {};
}

} // namespace

AskpassDialog::AskpassDialog(const QString& prompt, QWidget* parent)
    : QDialog(parent), request_(git::parseAskpassPrompt(prompt.toStdString())) {
    setWindowTitle(titleFor(request_.kind));
    setMinimumWidth(460);

    auto* layout = new QVBoxLayout(this);

    // git's own words, verbatim: it names the host being authenticated to, and
    // rewording that would mean deciding for the user which host they are
    // looking at.
    auto* promptLabel = new QLabel(prompt.trimmed(), this);
    promptLabel->setWordWrap(true);
    promptLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(promptLabel);

    field_ = new QLineEdit(this);
    if (!request_.echo()) {
        field_->setEchoMode(QLineEdit::Password);
    }
    if (request_.kind == git::AskpassKind::HostKey) {
        // Deliberately not a yes/no button pair: ssh wants the word typed, and
        // typing it is the point. This is the one prompt where making the
        // answer effortless would be a disservice.
        field_->setPlaceholderText(QCoreApplication::translate("askpass", "Type yes or no"));
    }
    layout->addWidget(field_);

    QString note = noteFor(request_.kind);
    // GitHub stopped accepting account passwords for git in 2021: a password
    // typed here can only fail. Said before it is typed, not after.
    if (request_.kind == git::AskpassKind::Password &&
        QString::fromStdString(request_.subject).contains(QStringLiteral("github.com"),
                                                          Qt::CaseInsensitive)) {
        note = QCoreApplication::translate(
            "askpass", "GitHub does not accept your account password here — only a personal "
                       "access token. Usually this prompt means your sign-in did not answer for "
                       "this remote: check Tools \u25b8 Credentials in Gity.");
    }
    if (!note.isEmpty()) {
        auto* noteLabel = new QLabel(note, this);
        noteLabel->setWordWrap(true);
        QFont smaller = noteLabel->font();
        smaller.setPointSize(std::max(7, smaller.pointSize() - 1));
        noteLabel->setFont(smaller);
        applyRole(noteLabel, QStringLiteral("note"));
        layout->addWidget(noteLabel);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    field_->setFocus();
}

QString AskpassDialog::answer() const {
    return field_->text();
}

} // namespace gity::ui
