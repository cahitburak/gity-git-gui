// MainWindow: the commands the user starts — from menus, the toolbar, the
// commit menu and the sidebar — and the dialogs that ask about them.
#include "MainWindow.h"

#include "core/git/Provider.h"
#include "core/git/PullRequest.h"
#include "session/Accounts.h"
#include "ui/panels/CloneDialog.h"
#include "ui/panels/Confirm.h"
#include "ui/panels/PreviewPrompt.h"
#include "ui/panels/RebaseDialog.h"
#include "ui/panels/RefsSidebar.h"
#include "ui/panels/SettingsDialog.h"
#include "ui/panels/SwitchDialogs.h"
#include "ui/theme/ToolbarIcons.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>
#include <git2.h>

namespace gity::app {

gity::git::Provider MainWindow::providerFor(const std::string& remoteUrl) {
    const gity::git::Provider guessed =
        gity::git::providerForHost(gity::git::hostOfRemote(remoteUrl));
    if (guessed != gity::git::Provider::Other) {
        return guessed;
    }
    // A self-hosted server with an unhelpful name — git.studio.io — is still
    // known if an account was added for it, with its provider chosen then.
    for (const auto& account :
         gity::session::Accounts::forRemote(QString::fromStdString(remoteUrl))) {
        if (account.provider != gity::git::Provider::Other) {
            return account.provider;
        }
    }
    return gity::git::Provider::Other;
}

void MainWindow::openPullRequestPage(const QString& remoteBranch) {
    if (!currentRemotes_) {
        return;
    }
    // The remote is the longest remote name the branch starts with, so a
    // remote called "team/a" is not mistaken for "team".
    const gity::git::RemoteEntry* remote = nullptr;
    for (const auto& candidate : *currentRemotes_) {
        const QString prefix = QString::fromStdString(candidate.name) + QChar('/');
        if (remoteBranch.startsWith(prefix) &&
            (remote == nullptr || candidate.name.size() > remote->name.size())) {
            remote = &candidate;
        }
    }
    if (remote == nullptr) {
        return;
    }
    const QString remoteName = QString::fromStdString(remote->name);
    const QString source = remoteBranch.mid(remoteName.size() + 1);
    // Into the remote's default branch, as its HEAD names it — the release
    // branch, in a project that makes each release the default.
    QString target;
    if (currentRefs_) {
        for (const auto& name : currentRefs_->remoteDefaults) {
            const QString full = QString::fromStdString(name);
            if (full.startsWith(remoteName + QChar('/'))) {
                target = full.mid(remoteName.size() + 1);
            }
        }
    }
    if (target == source) {
        target.clear(); // the host will ask
    }
    const auto url = gity::git::newPullRequestUrl(providerFor(remote->fetchUrl), remote->fetchUrl,
                                                  source.toStdString(), target.toStdString());
    if (!url) {
        statusBar()->showMessage(
            tr("%1's host is not one Gity knows how to open a page on.").arg(remoteName), 6000);
        return;
    }
    QDesktopServices::openUrl(QUrl(QString::fromStdString(*url), QUrl::StrictMode));
    statusBar()->showMessage(tr("Opened the new %1 page for %2 in your browser.")
                                 .arg(QString::fromStdString(std::string(gity::git::pullRequestNoun(
                                          providerFor(remote->fetchUrl)))),
                                      source),
                             6000);
}

void MainWindow::onCommitContextMenu(qsizetype row, const QPoint& globalPos) {
    const QString oid = model_.fullId(static_cast<std::size_t>(row));
    if (oid.isEmpty()) {
        return;
    }
    const QString shortId = oid.left(8);

    using gity::ui::icons::Glyph;
    // Menu text, so the icons sit at the same weight as the words beside them.
    const auto glyph = [](Glyph which) { return gity::ui::icons::themed(which); };

    QMenu menu(this);
    // Both verbs act on the commit under the cursor, so both name it. A menu
    // that says only "Revert" leaves the user checking which row was selected.
    QAction* pick = menu.addAction(glyph(Glyph::CherryPick),
                                   tr("Cherry-pick %1 onto this branch").arg(shortId));
    QAction* revert = menu.addAction(glyph(Glyph::Revert), tr("Revert %1").arg(shortId));
    menu.addSeparator();
    QAction* rebase =
        menu.addAction(glyph(Glyph::Rebase), tr("Rebase the commits above %1…").arg(shortId));
    menu.addSeparator();
    QAction* tagHere = menu.addAction(glyph(Glyph::Tag), tr("Tag %1…").arg(shortId));
    QAction* reset =
        menu.addAction(glyph(Glyph::Reset), tr("Reset this branch to %1…").arg(shortId));
    menu.addSeparator();
    QAction* copy = menu.addAction(glyph(Glyph::Copy), tr("Copy Commit SHA"));

    using gity::ui::describeCommand;
    menu.setToolTipsVisible(true);
    describeCommand(pick, tr("Apply this commit's change on top of the current branch."),
                    QStringLiteral("cherry-pick %1").arg(shortId));
    describeCommand(revert, tr("Add a new commit that undoes this one."),
                    QStringLiteral("revert %1").arg(shortId));
    describeCommand(rebase, tr("Reorder, squash, edit or drop the commits after this one."),
                    QStringLiteral("rebase -i %1").arg(shortId));
    describeCommand(tagHere, tr("Name this commit."), QStringLiteral("tag <name> %1").arg(shortId));
    describeCommand(reset, tr("Move the current branch to this commit."),
                    QStringLiteral("reset --soft|--mixed|--hard %1").arg(shortId));

    QAction* chosen = menu.exec(globalPos);
    if (chosen == nullptr) {
        return;
    }
    if (chosen == copy) {
        QGuiApplication::clipboard()->setText(oid);
        return;
    }
    if (chosen == pick) {
        session()->requestCherryPick(oid);
        return;
    }
    if (chosen == revert) {
        // Revert writes a commit; cherry-pick does too. Neither rewrites
        // history, so neither is confirmed — both are undone by reverting
        // again or resetting, and a confirmation on every ordinary action
        // teaches people to click through them.
        session()->requestRevert(oid);
        return;
    }
    if (chosen == rebase) {
        startRebase(oid, model_.summary(static_cast<std::size_t>(row)));
        return;
    }
    if (chosen == tagHere) {
        tagTarget_ = oid;
        chooseNewTag();
        return;
    }
    if (chosen == reset) {
        confirmReset(oid, model_.summary(static_cast<std::size_t>(row)));
    }
}

void MainWindow::chooseNewTag() {
    if (currentWorkdir_.isEmpty()) {
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("New Tag"));
    dialog.setMinimumWidth(420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;

    auto* name = new QLineEdit(&dialog);
    name->setPlaceholderText(QStringLiteral("v1.0.0"));
    form->addRow(tr("Name"), name);

    auto* message = new QLineEdit(&dialog);
    message->setPlaceholderText(tr("Optional — leave empty for a lightweight tag"));
    form->addRow(tr("Message"), message);
    layout->addLayout(form);

    auto* note = new QLabel(
        tr("A message makes this an annotated tag: an object of its own carrying who made it "
           "and when, which is what a release wants. Without one it is a bare pointer at the "
           "commit.\n\nTags are not pushed by Push — right-click the tag to send it."),
        &dialog);
    note->setWordWrap(true);
    gity::ui::applyRole(note, QStringLiteral("note"));
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted || name->text().trimmed().isEmpty()) {
        return;
    }
    session()->requestCreateTag(name->text().trimmed(), tagTarget_, message->text().trimmed());
    tagTarget_.clear();
}

void MainWindow::confirmReset(const QString& oid, const QString& subject) {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Reset Branch"));
    dialog.setMinimumWidth(520);
    auto* layout = new QVBoxLayout(&dialog);

    auto* intro = new QLabel(
        tr("Move this branch to %1 — \"%2\".\n\nCommits after it stay reachable through the "
           "reflog, so this is recoverable; what it does to your working copy is not.")
            .arg(oid.left(8), subject),
        &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // Each mode says what happens to the two things people actually care
    // about, because "soft, mixed, hard" means nothing until you have been
    // bitten by picking the wrong one.
    auto* soft = new QRadioButton(tr("Soft — keep all changes, staged"), &dialog);
    auto* mixed = new QRadioButton(tr("Mixed — keep all changes, unstaged"), &dialog);
    auto* hard = new QRadioButton(tr("Hard — discard all changes"), &dialog);
    mixed->setChecked(true);
    gity::ui::applyRole(hard, QStringLiteral("error"));
    layout->addWidget(soft);
    layout->addWidget(mixed);
    layout->addWidget(hard);

    auto* warning = new QLabel(&dialog);
    warning->setWordWrap(true);
    gity::ui::applyRole(warning, QStringLiteral("warn"));
    layout->addWidget(warning);
    const auto updateWarning = [warning, hard] {
        warning->setText(hard->isChecked()
                             ? tr("Uncommitted work in the working copy is thrown away. Gity "
                                  "snapshots it first, so Edit ▸ Undo brings it back — but "
                                  "nothing outside Gity can.")
                             : QString());
    };
    connect(hard, &QRadioButton::toggled, &dialog, updateWarning);
    updateWarning();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    QPushButton* confirm = buttons->addButton(tr("Reset"), QDialogButtonBox::AcceptRole);
    // Reset is only destructive in its hard mode, so the button follows the
    // choice rather than shouting at someone doing a soft one.
    const auto markMode = [confirm, hard] {
        gity::ui::applyRole(confirm, hard->isChecked() ? QStringLiteral("destructive")
                                                       : QStringLiteral("primary"));
    };
    connect(hard, &QRadioButton::toggled, &dialog, markMode);
    markMode();
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    session()->requestReset(oid, soft->isChecked()   ? QStringLiteral("soft")
                                 : hard->isChecked() ? QStringLiteral("hard")
                                                     : QStringLiteral("mixed"));
}

void MainWindow::startRebase(const QString& baseOid, const QString& baseSubject) {
    gity::ui::RebaseDialog dialog(*session(), baseOid, baseSubject, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    // Rebase rewrites commits, unlike every other verb offered here. It gets a
    // confirmation, and the confirmation says the thing that matters: shared
    // commits become different commits, and anyone who has them keeps the old
    // ones.
    const auto answer = QMessageBox::question(
        this, tr("Rewrite these commits?"),
        tr("A rebase replaces the commits above %1 with new ones. If any of them have been "
           "pushed, everyone else keeps the originals and the two histories diverge.\n\n"
           "Nothing is lost either way — the old commits stay reachable through the reflog.")
            .arg(baseOid.left(8)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    session()->requestInteractiveRebase(dialog.baseOid(), dialog.steps());
}

gity::session::SwitchContext MainWindow::switchContext(const QString& ref) const {
    return gity::session::switchContext(currentRefs_.get(), currentStatus_.get(), ref);
}

void MainWindow::confirmCheckout(const QString& ref) {
    if (currentWorkdir_.isEmpty()) {
        return;
    }
    // What is being switched to: a remote branch becomes a local one tracking
    // it; a tag or commit is checked out detached.
    const gity::session::SwitchContext context = switchContext(ref);

    // Nothing to decide: switch, and bring submodules along.
    if (changedFiles_ == 0) {
        session()->requestCheckout(ref, context.kind, gity::session::LocalChanges::Keep,
                                   context.hasSubmodules);
        return;
    }
    // Asked before switching rather than discovered after: what to do with
    // the changes, and whether submodules follow the branch.
    gity::ui::CheckoutDialog dialog(
        ref, context, changedFiles_,
        currentRefs_ ? QString::fromStdString(currentRefs_->headName) : QString(), this);
    if (dialog.exec() == QDialog::Accepted) {
        session()->requestCheckout(ref, context.kind, dialog.changes(), dialog.updateSubmodules());
    }
}

void MainWindow::openNewBranchDialog(const QString& base) {
    if (currentWorkdir_.isEmpty()) {
        return;
    }
    const gity::session::SwitchContext context = switchContext(base);
    QStringList existing;
    if (currentRefs_) {
        for (const auto& branch : currentRefs_->localBranches) {
            existing << QString::fromStdString(branch.name);
        }
    }
    gity::ui::NewBranchDialog dialog(base, context, changedFiles_, existing, this);
    if (dialog.exec() == QDialog::Accepted) {
        session()->requestCreateBranch(dialog.name(), base, context.kind, dialog.switchesTo(),
                                       dialog.changes(), dialog.updateSubmodules());
    }
}

void MainWindow::chooseStash() {
    if (currentWorkdir_.isEmpty()) {
        return;
    }
    if (changedFiles_ == 0) {
        QMessageBox::information(this, tr("Nothing to stash"),
                                 tr("The working copy has no changes."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Stash Changes"));
    dialog.setMinimumWidth(460);
    auto* layout = new QVBoxLayout(&dialog);

    auto* label = new QLabel(tr("Set the changes aside and return the working copy to the "
                                "last commit. Nothing is lost — they come back with Pop."),
                             &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto* message = new QLineEdit(&dialog);
    message->setPlaceholderText(tr("Description (optional)"));
    layout->addWidget(message);

    auto* untracked = new QCheckBox(tr("Include new, untracked files"), &dialog);
    // On by default: a stash that leaves new files behind is not the whole
    // change, and finding that out later is the expensive way to learn it.
    untracked->setChecked(true);
    layout->addWidget(untracked);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    QPushButton* save = buttons->addButton(tr("Stash"), QDialogButtonBox::AcceptRole);
    Q_UNUSED(save)
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    session()->requestStashSave(message->text().trimmed(), untracked->isChecked());
}

void MainWindow::chooseNewBranch() {
    openNewBranchDialog(QString());
}

void MainWindow::chooseComparison() {
    if (currentWorkdir_.isEmpty() || !currentRefs_) {
        return;
    }
    // Every branch and tag, the checked-out branch first.
    QStringList names;
    QStringList oids;
    const auto add = [&names, &oids](const std::vector<gity::git::RefEntry>& entries) {
        for (const auto& entry : entries) {
            char buffer[GIT_OID_SHA1_HEXSIZE + 1] = {};
            git_oid_tostr(buffer, sizeof(buffer), &entry.target);
            names << QString::fromStdString(entry.name);
            oids << QString::fromLatin1(buffer);
        }
    };
    add(currentRefs_->localBranches);
    add(currentRefs_->remoteBranches);
    add(currentRefs_->tags);
    if (names.size() < 2) {
        QMessageBox::information(this, tr("Compare Branches"),
                                 tr("There is only one branch, so nothing to compare it with."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Compare Branches"));
    dialog.setMinimumWidth(440);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* from = new QComboBox(&dialog);
    auto* to = new QComboBox(&dialog);
    from->addItems(names);
    to->addItems(names);
    from->setCurrentIndex(0);
    to->setCurrentIndex(1);
    form->addRow(tr("From"), from);
    form->addRow(tr("To"), to);
    layout->addLayout(form);
    auto* note = new QLabel(tr("Shows what changes going from the first to the second — tip to "
                               "tip — and the commits each has that the other does not."),
                            &dialog);
    note->setWordWrap(true);
    gity::ui::applyRole(note, QStringLiteral("note"));
    layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    QPushButton* compare = buttons->addButton(tr("Compare"), QDialogButtonBox::AcceptRole);
    gity::ui::applyRole(compare, QStringLiteral("primary"));
    compare->setDefault(true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || from->currentIndex() == to->currentIndex()) {
        return;
    }
    showScreen(gity::ui::RefsSidebar::Screen::History);
    startComparison(names.at(from->currentIndex()), oids.at(from->currentIndex()),
                    names.at(to->currentIndex()), oids.at(to->currentIndex()));
}

void MainWindow::chooseMerge() {
    if (currentWorkdir_.isEmpty() || !refs_) {
        return;
    }
    const gity::session::RefSetPtr refs = currentRefs_;
    if (!refs) {
        return;
    }

    QStringList candidates;
    QString head;
    for (const auto& branch : refs->localBranches) {
        if (branch.isHead) {
            head = QString::fromStdString(branch.name);
            continue; // merging a branch into itself is not an offer worth making
        }
        candidates << QString::fromStdString(branch.name);
    }
    for (const auto& branch : refs->remoteBranches) {
        candidates << QString::fromStdString(branch.name);
    }
    if (candidates.isEmpty()) {
        QMessageBox::information(this, tr("Nothing to merge"),
                                 tr("This repository has no other branch to merge from."));
        return;
    }

    bool accepted = false;
    const QString chosen =
        QInputDialog::getItem(this, tr("Merge"), tr("Merge which branch into %1?").arg(head),
                              candidates, 0, false, &accepted);
    if (!accepted || chosen.isEmpty()) {
        return;
    }
    askPreview(gity::session::Preview::Kind::Merge, chosen);
}

void MainWindow::openSettings(SettingsPage page) {
    // The origin URL is what the Accounts section binds to an account, so it
    // is looked up now rather than remembered: a remote can be changed from
    // the Remotes dialog while this window is open.
    QString originUrl;
    if (currentRemotes_) {
        for (const auto& remote : *currentRemotes_) {
            if (remote.name == "origin") {
                originUrl = QString::fromStdString(remote.fetchUrl);
                break;
            }
        }
        if (originUrl.isEmpty() && !currentRemotes_->empty()) {
            originUrl = QString::fromStdString(currentRemotes_->front().fetchUrl);
        }
    }

    QList<QPair<QString, QString>> remotes;
    if (currentRemotes_) {
        for (const auto& remote : *currentRemotes_) {
            remotes.append(
                {QString::fromStdString(remote.name), QString::fromStdString(remote.fetchUrl)});
        }
    }
    gity::ui::SettingsDialog dialog(currentWorkdir_, originUrl, remotes, this);
    // One way to choose a repository's account: its own credential config,
    // for it and its submodules (RepoWorker::setAccountChoice). Accounts'
    // "Use for this repository" used to rewrite the remote URL instead, which
    // GitHub CLI ignores for any account but its active one.
    const auto assign = [this](const QString& account) { session()->requestSetAccount(account); };
    connect(&dialog, &gity::ui::SettingsDialog::useAccountForRepository, this, assign);
    connect(&dialog, &gity::ui::SettingsDialog::repositoryAccountChosen, this, assign);
    dialog.setRepositoryAccount(account_.host, account_.viaGitHubCli, account_.accounts,
                                account_.defaultAccount, account_.chosen);
    if (page == SettingsPage::Accounts) {
        dialog.showAccounts();
    } else if (page == SettingsPage::Credentials) {
        dialog.showCredentials();
    } else if (page == SettingsPage::Appearance) {
        dialog.showAppearance();
    }
    dialog.exec();
}

void MainWindow::chooseInteractiveRebase() {
    if (currentWorkdir_.isEmpty()) {
        return;
    }
    // The bases come from git, not from the graph: the graph's rows include
    // every other branch, and a filter's matches, and a base that is not an
    // ancestor of HEAD rebases nothing the user meant.
    awaitingRebaseBases_ = true;
    session()->requestHeadAncestors(31);
}

void MainWindow::offerInteractiveRebase(const QStringList& oids, const QStringList& subjects) {
    if (!awaitingRebaseBases_) {
        return;
    }
    awaitingRebaseBases_ = false;

    // Row 0 is HEAD itself, which has nothing above it to rebase.
    QStringList choices;
    std::vector<qsizetype> rows;
    for (qsizetype row = 1; row < oids.size(); ++row) {
        choices << tr("%1  %2").arg(oids.at(row).left(7), subjects.at(row));
        rows.push_back(row);
    }
    if (choices.isEmpty()) {
        QMessageBox::information(
            this, tr("Nothing to rebase"),
            tr("There is only one commit, so there is nothing above it to reorder."));
        return;
    }

    // "Rebase everything above this one" is the question an interactive
    // rebase actually answers, so the base is picked rather than counted.
    bool accepted = false;
    const QString chosen = QInputDialog::getItem(
        this, tr("Interactive Rebase"),
        tr("Rebase the commits above which one?\n"
           "Everything newer than the commit you pick can be reordered, squashed or dropped."),
        choices, 0, false, &accepted);
    if (!accepted || chosen.isEmpty()) {
        return;
    }
    const qsizetype index = choices.indexOf(chosen);
    if (index < 0) {
        return;
    }
    const qsizetype row = rows[static_cast<std::size_t>(index)];
    startRebase(oids.at(row), subjects.at(row));
}

void MainWindow::chooseRebase() {
    if (currentWorkdir_.isEmpty() || !currentRefs_) {
        return;
    }

    QStringList candidates;
    QString head;
    for (const auto& branch : currentRefs_->localBranches) {
        if (branch.isHead) {
            head = QString::fromStdString(branch.name);
            continue;
        }
        candidates << QString::fromStdString(branch.name);
    }
    for (const auto& branch : currentRefs_->remoteBranches) {
        candidates << QString::fromStdString(branch.name);
    }
    if (candidates.isEmpty()) {
        QMessageBox::information(this, tr("Nothing to rebase onto"),
                                 tr("This repository has no other branch to rebase onto."));
        return;
    }

    bool accepted = false;
    const QString onto =
        QInputDialog::getItem(this, tr("Rebase"), tr("Replay %1 on top of which branch?").arg(head),
                              candidates, 0, false, &accepted);
    if (!accepted || onto.isEmpty()) {
        return;
    }

    askPreview(gity::session::Preview::Kind::Rebase, onto);
}

void MainWindow::askPreview(gity::session::Preview::Kind kind, const QString& target,
                            const QString& branch) {
    if (currentWorkdir_.isEmpty()) {
        return;
    }
    statusBar()->showMessage(kind == gity::session::Preview::Kind::Pull
                                 ? tr("Checking the remote…")
                                 : tr("Working out what will happen…"));
    session()->requestPreview(kind, target, branch);
}

void MainWindow::showPreview(gity::session::PreviewPtr preview) {
    using Kind = gity::session::Preview::Kind;
    using gity::ui::PreviewChoice;
    statusBar()->clearMessage();
    if (!preview) {
        return;
    }
    const auto& p = *preview;
    switch (gity::ui::choosePreviewAction(p, this)) {
    case PreviewChoice::Go:
        if (p.kind == Kind::Pull) {
            session()->requestPull();
        } else if (p.kind == Kind::Merge) {
            session()->requestMerge(p.target);
        } else if (p.kind == Kind::Rebase) {
            session()->requestRebaseOnto(p.target);
        } else {
            // The checked-out branch goes by plain `git push`, so push.default
            // and pushRemote decide as in a terminal; any other branch is
            // pushed by name.
            bool current = false;
            if (currentRefs_) {
                for (const auto& branch : currentRefs_->localBranches) {
                    current = current ||
                              (branch.isHead && QString::fromStdString(branch.name) == p.branch);
                }
            }
            if (current && !p.newBranch) {
                session()->requestPush();
            } else {
                session()->requestPushRef(p.branch);
            }
        }
        break;
    case PreviewChoice::MergeInstead:
        session()->requestMerge(p.target);
        break;
    case PreviewChoice::RebaseInstead:
        session()->requestRebaseOnto(p.target);
        break;
    case PreviewChoice::Force:
        session()->requestForcePush(p.branch, p.remote, p.remoteBranch, p.leaseOid);
        break;
    case PreviewChoice::Cancel:
        break;
    }
}

void MainWindow::onHistoryEditFinished(const QString& verb, bool ok, bool conflicted,
                                       const QString& message) {
    // A branch that holds commits nothing else references cannot be deleted
    // with -d, and git says so. Refusing to go further was the wrong call:
    // deleting a scratch branch is routine, and a client that can only report
    // the refusal makes the user go to a terminal to finish an ordinary job.
    // The safety is in making it an informed choice, not in blocking it.
    if (!ok && !pendingBranchDelete_.isEmpty() &&
        message.contains(QStringLiteral("not fully merged"))) {
        const QString name = pendingBranchDelete_;
        pendingBranchDelete_.clear();

        if (gity::ui::confirmDestructive(
                this, tr("Delete anyway?"),
                tr("\"%1\" holds commits that are not on any other branch.").arg(name),
                tr("Deleting it leaves those commits reachable only through the reflog, "
                   "which expires — after that they are gone. If you meant to keep the "
                   "work, merge it somewhere first."),
                tr("Delete Anyway"))) {
            session()->requestDeleteBranch(name, true);
        }
        return;
    }
    pendingBranchDelete_.clear();

    if (ok) {
        statusBar()->showMessage(message.isEmpty() ? tr("%1 finished").arg(verb) : message, 8000);
        openRepository(currentWorkdir_);
        return;
    }
    if (conflicted) {
        // The banner already says what to do and stays until it is resolved,
        // so a modal here would only be a second thing to dismiss.
        statusBar()->showMessage(message, 12000);
        return;
    }
    QMessageBox::warning(this, tr("%1 failed").arg(verb), message);
}

void MainWindow::chooseClone() {
    gity::ui::CloneDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    session()->requestClone(dialog.url(), dialog.parentDirectory(), dialog.folderName());
}

} // namespace gity::app
