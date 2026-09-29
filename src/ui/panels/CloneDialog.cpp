#include "CloneDialog.h"

#include "ui/panels/Confirm.h"

#include "core/git/RemoteUrl.h"
#include "ui/theme/Tokens.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace gity::ui {

QString CloneDialog::folderNameFor(const QString& url) {
    // The rule itself lives in core, where it is tested against the shapes a
    // remote URL actually arrives in.
    return QString::fromStdString(gity::git::folderNameForRemote(url.toStdString()));
}

CloneDialog::CloneDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Clone Repository"));
    setModal(true);
    setMinimumWidth(520);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    url_ = new QLineEdit(this);
    url_->setPlaceholderText(QStringLiteral("https://github.com/owner/project.git"));
    form->addRow(tr("Remote URL"), url_);

    auto* directoryRow = new QWidget(this);
    auto* directoryLayout = new QHBoxLayout(directoryRow);
    directoryLayout->setContentsMargins(0, 0, 0, 0);
    directory_ = new QLineEdit(directoryRow);
    directory_->setText(
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
    directoryLayout->addWidget(directory_, 1);
    auto* browse = new QPushButton(tr("Browse…"), directoryRow);
    directoryLayout->addWidget(browse);
    form->addRow(tr("Clone into"), directoryRow);

    folder_ = new QLineEdit(this);
    form->addRow(tr("Folder name"), folder_);

    layout->addLayout(form);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    layout->addWidget(status_);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->button(QDialogButtonBox::Ok)->setText(tr("Clone"));
    layout->addWidget(buttons_);

    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString chosen =
            QFileDialog::getExistingDirectory(this, tr("Clone Into"), directory_->text());
        if (!chosen.isEmpty()) {
            directory_->setText(chosen);
            revalidate();
        }
    });

    connect(url_, &QLineEdit::textChanged, this, [this](const QString& text) {
        // Follow the URL until the user takes the folder over themselves.
        if (!folderEdited_) {
            const QSignalBlocker blocker(folder_);
            folder_->setText(folderNameFor(text));
        }
        revalidate();
    });
    connect(folder_, &QLineEdit::textEdited, this, [this] {
        folderEdited_ = true;
        revalidate();
    });
    connect(directory_, &QLineEdit::textChanged, this, &CloneDialog::revalidate);

    revalidate();
}

QString CloneDialog::url() const {
    return url_->text().trimmed();
}

QString CloneDialog::parentDirectory() const {
    return directory_->text().trimmed();
}

QString CloneDialog::folderName() const {
    return folder_->text().trimmed();
}

void CloneDialog::revalidate() {
    const auto refuse = [this](const QString& reason) {
        status_->setText(reason);
        applyRole(status_, QStringLiteral("warn"));
        buttons_->button(QDialogButtonBox::Ok)->setEnabled(false);
    };

    if (url().isEmpty()) {
        status_->clear();
        buttons_->button(QDialogButtonBox::Ok)->setEnabled(false);
        return;
    }
    if (folderName().isEmpty()) {
        refuse(tr("Give the folder a name."));
        return;
    }
    const QDir parent(parentDirectory());
    if (parentDirectory().isEmpty() || !parent.exists()) {
        refuse(tr("That folder does not exist."));
        return;
    }

    const QString target = parent.filePath(folderName());
    if (QFileInfo::exists(target)) {
        // git refuses this too, but only after the connection is made and the
        // credentials are entered, which is a poor moment to find out.
        refuse(tr("%1 already exists.").arg(QDir::toNativeSeparators(target)));
        return;
    }

    status_->setText(tr("Clones into %1").arg(QDir::toNativeSeparators(target)));
    applyRole(status_, QStringLiteral("note"));
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(true);
}

} // namespace gity::ui
