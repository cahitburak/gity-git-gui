#include "SwitchDialogs.h"

#include "Confirm.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace gity::ui {

using gity::session::LocalChanges;
using gity::session::RefKind;

ChangesChoice::ChangesChoice(QDialog* dialog, QVBoxLayout* layout, const QString& destination) {
    keep_ = new QRadioButton(tr("Keep changes — carry them to %1").arg(destination), dialog);
    stash_ =
        new QRadioButton(tr("Stash and reapply — set them aside, switch, bring them back"), dialog);
    discard_ = new QRadioButton(tr("Discard changes"), dialog);
    applyRole(discard_, QStringLiteral("error"));
    keep_->setChecked(true);
    for (QRadioButton* option : {keep_, stash_, discard_}) {
        layout->addWidget(option);
    }
    explain_ = new QLabel(dialog);
    explain_->setWordWrap(true);
    applyRole(explain_, QStringLiteral("note"));
    layout->addWidget(explain_);
}

LocalChanges ChangesChoice::value() const {
    return discard_->isChecked() ? LocalChanges::Discard
           : stash_->isChecked() ? LocalChanges::StashAndReapply
                                 : LocalChanges::Keep;
}

void ChangesChoice::setVisible(bool visible) const {
    for (QWidget* widget : {static_cast<QWidget*>(keep_), static_cast<QWidget*>(stash_),
                            static_cast<QWidget*>(discard_), static_cast<QWidget*>(explain_)}) {
        widget->setVisible(visible);
    }
}

void ChangesChoice::describe(QPushButton* go, const QString& text,
                             const QString& discardText) const {
    if (discard_->isChecked() && !discard_->isHidden()) {
        explain_->setText(tr("Uncommitted edits to tracked files are thrown away; new, "
                             "untracked files stay. Edit ▸ Undo brings the edits back."));
        go->setText(discardText);
        applyRole(go, QStringLiteral("destructive"));
        return;
    }
    explain_->setText(stash_->isChecked()
                          ? tr("If reapplying conflicts, the changes stay in the stash "
                               "until you resolve them.")
                          : tr("git refuses to switch if a change would be overwritten; "
                               "nothing is lost either way."));
    go->setText(text);
    applyRole(go, QStringLiteral("primary"));
}

void ChangesChoice::onChanged(QDialog* dialog, const std::function<void()>& changed) const {
    for (QRadioButton* option : {keep_, stash_, discard_}) {
        QObject::connect(option, &QRadioButton::toggled, dialog, changed);
    }
}

namespace {

QPushButton* addButtons(QDialog* dialog, QVBoxLayout* layout, const QString& text) {
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, dialog);
    QPushButton* go = buttons->addButton(text, QDialogButtonBox::AcceptRole);
    go->setDefault(true);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    return go;
}

} // namespace

CheckoutDialog::CheckoutDialog(const QString& ref, const gity::session::SwitchContext& context,
                               int changedFiles, const QString& headName, QWidget* parent)
    : QDialog(parent), hasSubmodules_(context.hasSubmodules) {
    // A remote branch is switched to as the local branch tracking it.
    const QString shown = context.kind == RefKind::Remote ? ref.section(QChar('/'), 1) : ref;
    setWindowTitle(tr("Switch to %1").arg(shown));
    setMinimumWidth(500);
    auto* layout = new QVBoxLayout(this);
    const bool onlySubmodules = context.submoduleChanges == changedFiles;
    auto* intro = new QLabel(
        onlySubmodules
            ? tr("Changed: %n submodule(s), checked out at a different commit than %1 records "
                 "— left behind by an earlier switch, not edits of yours. Updating submodules "
                 "after switching puts them where %2 records them.",
                 nullptr, changedFiles)
                  .arg(headName.isEmpty() ? tr("the current branch") : headName, shown)
            : tr("You have %n uncommitted change(s).", nullptr, changedFiles) +
                  (context.submoduleChanges > 0
                       ? QChar(' ') + tr("%n of them are submodules at another commit, which "
                                         "updating submodules takes care of.",
                                         nullptr, context.submoduleChanges)
                       : QString()),
        this);
    // Branch names, which may hold "<".
    setPlainText(intro);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    choice_ = std::make_unique<ChangesChoice>(this, layout, shown);

    update_ = new QCheckBox(tr("Update submodules to the commits %1 records").arg(shown), this);
    update_->setChecked(true);
    update_->setVisible(context.hasSubmodules);
    layout->addWidget(update_);

    QPushButton* go = addButtons(this, layout, tr("Switch"));
    const auto refresh = [this, go] {
        choice_->describe(go, tr("Switch"), tr("Discard and Switch"));
    };
    choice_->onChanged(this, refresh);
    refresh();
}

CheckoutDialog::~CheckoutDialog() = default;

LocalChanges CheckoutDialog::changes() const {
    return choice_->value();
}

bool CheckoutDialog::updateSubmodules() const {
    return hasSubmodules_ && update_->isChecked();
}

NewBranchDialog::NewBranchDialog(const QString& base, const gity::session::SwitchContext& context,
                                 int changedFiles, const QStringList& existing, QWidget* parent)
    : QDialog(parent), context_(context), existing_(existing),
      // The same choices a checkout offers, when switching would move the
      // working copy to another commit with changes in it.
      askChanges_(changedFiles > 0 && !context.sameCommit) {
    setWindowTitle(tr("New Branch"));
    setMinimumWidth(500);
    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        base.isEmpty() ? tr("Starts at the current commit.") : tr("Starts at %1.").arg(base), this);
    setPlainText(intro);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    name_ = new QLineEdit(this);
    name_->setPlaceholderText(tr("Branch name"));
    layout->addWidget(name_);
    problem_ = new QLabel(this);
    setPlainText(problem_);
    applyRole(problem_, QStringLiteral("warn"));
    problem_->setVisible(false);
    layout->addWidget(problem_);

    checkout_ = new QCheckBox(tr("Check out the new branch"), this);
    checkout_->setChecked(true);
    layout->addWidget(checkout_);

    changesIntro_ =
        new QLabel(tr("You have %n uncommitted change(s).", nullptr, changedFiles), this);
    changesIntro_->setWordWrap(true);
    layout->addWidget(changesIntro_);
    choice_ = std::make_unique<ChangesChoice>(this, layout, tr("the new branch"));

    update_ = new QCheckBox(
        tr("Update submodules to the commits %1 records").arg(base.isEmpty() ? tr("it") : base),
        this);
    update_->setChecked(true);
    layout->addWidget(update_);

    go_ = addButtons(this, layout, tr("Create"));

    connect(name_, &QLineEdit::textChanged, this, &NewBranchDialog::refresh);
    connect(checkout_, &QCheckBox::toggled, this, &NewBranchDialog::refresh);
    choice_->onChanged(this, [this] { refresh(); });
    refresh();
}

NewBranchDialog::~NewBranchDialog() = default;

void NewBranchDialog::refresh() {
    const bool switching = checkout_->isChecked();
    changesIntro_->setVisible(switching && askChanges_);
    choice_->setVisible(switching && askChanges_);
    update_->setVisible(switching && context_.hasSubmodules && !context_.sameCommit);
    choice_->describe(go_, switching ? tr("Create and Switch") : tr("Create"),
                      tr("Create, Discard and Switch"));

    // Refused here rather than by git after the dialog has gone.
    const QString wanted = name();
    QString why;
    if (existing_.contains(wanted)) {
        why = tr("A branch named %1 already exists.").arg(wanted);
    } else if (wanted.contains(QRegularExpression(QStringLiteral("\\s")))) {
        why = tr("Branch names cannot contain spaces.");
    }
    problem_->setText(why);
    problem_->setVisible(!why.isEmpty());
    go_->setEnabled(!wanted.isEmpty() && why.isEmpty());
    adjustSize();
}

QString NewBranchDialog::name() const {
    return name_->text().trimmed();
}

bool NewBranchDialog::switchesTo() const {
    return checkout_->isChecked();
}

LocalChanges NewBranchDialog::changes() const {
    return switchesTo() && askChanges_ ? choice_->value() : LocalChanges::Keep;
}

bool NewBranchDialog::updateSubmodules() const {
    return switchesTo() && context_.hasSubmodules && !context_.sameCommit && update_->isChecked();
}

} // namespace gity::ui
