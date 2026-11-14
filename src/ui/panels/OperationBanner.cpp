#include "OperationBanner.h"

#include "ui/panels/Confirm.h"
#include "ui/theme/Tokens.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace gity::ui {
OperationBanner::OperationBanner(QWidget* parent) : QWidget(parent) {
    // Warn, not remove-tinted: being mid-merge is a state to finish, not a
    // failure to recover from, and colouring it as an error would make an
    // ordinary conflict feel like data loss.
    setObjectName(QStringLiteral("operationBanner"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 9, 12, 9);
    layout->setSpacing(10);

    auto* glyph = new QLabel(QStringLiteral("!"), this);
    glyph->setObjectName(QStringLiteral("operationGlyph"));
    layout->addWidget(glyph);

    text_ = new QLabel(this);
    text_->setWordWrap(true);
    text_->setObjectName(QStringLiteral("operationText"));
    layout->addWidget(text_, 1);

    resolve_ = new QPushButton(this);
    resolve_->setToolTip(tr("Open the conflict editor on the files still conflicted."));
    applyRole(resolve_, QStringLiteral("primary"));
    layout->addWidget(resolve_);
    resolve_->setVisible(false);
    connect(resolve_, &QPushButton::clicked, this, &OperationBanner::resolveRequested);

    continue_ = new QPushButton(tr("Continue"), this);
    abort_ = new QPushButton(tr("Abandon"), this);
    // "Abandon", not "Abort": it is the word for what happens to the work, and
    // "abort" reads as an error button rather than a choice.
    // Finishing is the way forward; abandoning throws away what has been
    // resolved so far. The roles say so before either is read.
    applyRole(continue_, QStringLiteral("primary"));
    applyRole(abort_, QStringLiteral("destructive"));
    layout->addWidget(continue_);
    layout->addWidget(abort_);

    connect(continue_, &QPushButton::clicked, this, &OperationBanner::continueRequested);
    connect(abort_, &QPushButton::clicked, this, &OperationBanner::abortRequested);

    setVisible(false);
}

void OperationBanner::setConflictCount(int count) {
    if (count == conflicts_) {
        return;
    }
    conflicts_ = count;
    setOperation(operation_, noun_);
}

void OperationBanner::setEditing(const QString& subject) {
    if (subject == editing_) {
        return;
    }
    editing_ = subject;
    setOperation(operation_, noun_);
}

void OperationBanner::showResolve() {
    // While files are conflicted, resolving them is the way forward, so it
    // is the lit button and Continue waits its turn.
    const bool conflicted = conflicts_ > 0;
    resolve_->setText(tr("Resolve %n Conflict(s)…", nullptr, conflicts_));
    resolve_->setVisible(conflicted);
    applyRole(continue_, conflicted ? QString() : QStringLiteral("primary"));
}

void OperationBanner::setOperation(git::Operation operation, const QString& noun) {
    operation_ = operation;
    noun_ = noun;
    resolve_->setVisible(false);
    if (operation == git::Operation::None) {
        setVisible(false);
        return;
    }

    if (operation == git::Operation::Bisect) {
        // A bisect has no --continue/--abort in the sense the other two use,
        // so the buttons would lie about what they do.
        text_->setText(tr("A bisect is in progress. Finish it from a terminal — "
                          "git bisect good, bad or reset."));
        continue_->setVisible(false);
        abort_->setVisible(false);
        setVisible(true);
        return;
    }

    continue_->setVisible(true);
    abort_->setVisible(true);
    // The command behind each button, as for every other button in Gity.
    const QString command =
        QString::fromUtf8(git::operationCommand(operation).data(),
                          static_cast<qsizetype>(git::operationCommand(operation).size()));
    continue_->setToolTip(
        tr("Carry on with the staged resolution.\n\ngit %1 --continue").arg(command));
    abort_->setToolTip(tr("Go back to where you were before it started, throwing away what "
                          "has been resolved. Edit ▸ Undo can bring that back.\n\ngit %1 "
                          "--abort")
                           .arg(command));
    if (!editing_.isEmpty()) {
        text_->setText(tr("Stopped to edit \"%1\". Its changes are staged in Local Changes: "
                          "change, stage or unstage them, then Continue — it recommits them "
                          "with the message in the commit box and carries on with the rebase.")
                           .arg(editing_));
        showResolve();
        setVisible(true);
        return;
    }
    text_->setText(tr("You are in the middle of %1. Resolve any conflicts and stage the "
                      "results, then continue — or abandon it to return to where you were.")
                       .arg(noun));
    showResolve();
    setVisible(true);
}

} // namespace gity::ui
