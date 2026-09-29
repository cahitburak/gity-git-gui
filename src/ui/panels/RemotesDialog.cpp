#include "RemotesDialog.h"

#include "ui/panels/Confirm.h"

#include "core/git/Remotes.h"
#include "ui/theme/Tokens.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace gity::ui {
namespace {

QString describe(gity::git::RemoteNameProblem problem) {
    switch (problem) {
    case gity::git::RemoteNameProblem::Empty:
        return QCoreApplication::translate("RemotesDialog", "A remote needs a name.");
    case gity::git::RemoteNameProblem::Whitespace:
        return QCoreApplication::translate("RemotesDialog",
                                           "Remote names cannot contain spaces.");
    case gity::git::RemoteNameProblem::IllegalCharacter:
        return QCoreApplication::translate(
            "RemotesDialog", "git stores a remote as a ref namespace, and that name contains "
                             "a character refs cannot hold.");
    case gity::git::RemoteNameProblem::LeadingOrTrailingDot:
        return QCoreApplication::translate("RemotesDialog",
                                           "Remote names cannot begin or end with a dot.");
    case gity::git::RemoteNameProblem::AlreadyExists:
        return QCoreApplication::translate("RemotesDialog", "There is already a remote with "
                                                            "that name.");
    case gity::git::RemoteNameProblem::None:
        break;
    }
    return {};
}

} // namespace

RemotesDialog::RemotesDialog(session::RepoSession& session, QWidget* parent)
    : QDialog(parent), session_(session) {
    setWindowTitle(tr("Remotes"));
    setMinimumSize(620, 320);

    auto* layout = new QVBoxLayout(this);

    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({tr("Name"), tr("URL")});
    tree_->setRootIsDecorated(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(tree_, 1);

    error_ = new QLabel(this);
    error_->setWordWrap(true);
    applyRole(error_, QStringLiteral("error"));
    error_->setVisible(false);
    layout->addWidget(error_);

    auto* buttonRow = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add…"), this);
    rename_ = new QPushButton(tr("Rename…"), this);
    editUrl_ = new QPushButton(tr("Change URL…"), this);
    remove_ = new QPushButton(tr("Remove"), this);
    buttonRow->addWidget(add);
    buttonRow->addWidget(rename_);
    buttonRow->addWidget(editUrl_);
    buttonRow->addWidget(remove_);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::accept);

    connect(add, &QPushButton::clicked, this, &RemotesDialog::addRemote);
    connect(rename_, &QPushButton::clicked, this, &RemotesDialog::renameSelected);
    connect(remove_, &QPushButton::clicked, this, &RemotesDialog::removeSelected);
    connect(editUrl_, &QPushButton::clicked, this, &RemotesDialog::editUrlOfSelected);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &RemotesDialog::updateButtons);

    connect(&session_, &session::RepoSession::remotesReady, this, &RemotesDialog::setRemotes);
    connect(&session_, &session::RepoSession::remoteCommandFailed, this,
            [this](const QString& message) {
                error_->setText(message);
                error_->setVisible(true);
            });

    session_.requestRemotes();
    updateButtons();
}

void RemotesDialog::setRemotes(session::RemoteListPtr remotes) {
    const QString previous = selectedName();
    remotes_ = std::move(remotes);
    tree_->clear();
    if (!remotes_) {
        updateButtons();
        return;
    }

    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    for (const auto& remote : *remotes_) {
        auto* item = new QTreeWidgetItem(tree_);
        item->setText(0, QString::fromStdString(remote.name));
        item->setText(1, QString::fromStdString(remote.fetchUrl));
        item->setFont(1, mono);

        if (remote.pushesElsewhere()) {
            // Its own row, because a push URL that differs is the difference
            // worth noticing on this screen.
            auto* push = new QTreeWidgetItem(item);
            push->setText(0, tr("pushes to"));
            push->setText(1, QString::fromStdString(remote.pushUrl));
            push->setFont(1, mono);
            push->setForeground(0, tokens::semanticWarn);
            item->setExpanded(true);
        }

        if (QString::fromStdString(remote.name) == previous) {
            tree_->setCurrentItem(item);
        }
    }
    updateButtons();
}

QString RemotesDialog::selectedName() const {
    QTreeWidgetItem* item = tree_->currentItem();
    if (item == nullptr) {
        return {};
    }
    // A push-URL child stands for its parent remote.
    if (item->parent() != nullptr) {
        item = item->parent();
    }
    return item->text(0);
}

void RemotesDialog::updateButtons() {
    const bool has = !selectedName().isEmpty();
    rename_->setEnabled(has);
    remove_->setEnabled(has);
    editUrl_->setEnabled(has);
}

QString RemotesDialog::askForName(const QString& title, const QString& initial) {
    QString name = initial;
    while (true) {
        bool accepted = false;
        name = QInputDialog::getText(this, title, tr("Name"), QLineEdit::Normal, name,
                                     &accepted)
                   .trimmed();
        if (!accepted) {
            return {};
        }
        const std::vector<gity::git::RemoteEntry> existing =
            remotes_ ? *remotes_ : std::vector<gity::git::RemoteEntry>{};
        const auto problem = gity::git::checkRemoteName(name.toStdString(), existing);
        if (problem == gity::git::RemoteNameProblem::None) {
            return name;
        }
        // Re-ask with the text still there rather than dismissing and making
        // them start over.
        QMessageBox::warning(this, title, describe(problem));
    }
}

void RemotesDialog::addRemote() {
    error_->setVisible(false);
    const QString name = askForName(tr("Add Remote"), QStringLiteral("origin"));
    if (name.isEmpty()) {
        return;
    }
    bool accepted = false;
    const QString url =
        QInputDialog::getText(this, tr("Add Remote"), tr("URL"), QLineEdit::Normal,
                              QString(), &accepted)
            .trimmed();
    if (!accepted || url.isEmpty()) {
        return;
    }
    changed_ = true;
    session_.requestAddRemote(name, url);
}

void RemotesDialog::renameSelected() {
    error_->setVisible(false);
    const QString current = selectedName();
    if (current.isEmpty()) {
        return;
    }
    const QString name = askForName(tr("Rename Remote"), current);
    if (name.isEmpty() || name == current) {
        return;
    }
    changed_ = true;
    session_.requestRenameRemote(current, name);
}

void RemotesDialog::editUrlOfSelected() {
    error_->setVisible(false);
    const QString name = selectedName();
    if (name.isEmpty() || !remotes_) {
        return;
    }
    QString current;
    for (const auto& remote : *remotes_) {
        if (QString::fromStdString(remote.name) == name) {
            current = QString::fromStdString(remote.fetchUrl);
            break;
        }
    }
    bool accepted = false;
    const QString url = QInputDialog::getText(this, tr("Change URL"), tr("URL for %1").arg(name),
                                              QLineEdit::Normal, current, &accepted)
                            .trimmed();
    if (!accepted || url.isEmpty() || url == current) {
        return;
    }
    changed_ = true;
    session_.requestSetRemoteUrl(name, url);
}

void RemotesDialog::removeSelected() {
    error_->setVisible(false);
    const QString name = selectedName();
    if (name.isEmpty()) {
        return;
    }
    // Confirmed, because it takes the remote's tracking refs with it — the
    // commits survive, but the branches you were reading them through do not.
    const auto answer = QMessageBox::question(
        this, tr("Remove Remote"),
        tr("Remove \"%1\"?\n\nIts remote-tracking branches go with it. Nothing is deleted on "
           "the server, and no commit of yours is lost.")
            .arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    changed_ = true;
    session_.requestRemoveRemote(name);
}

} // namespace gity::ui
