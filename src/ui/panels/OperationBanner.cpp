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

void OperationBanner::setEditing(const QString& subject) {
    if (subject == editing_) {
        return;
    }
    editing_ = subject;
    setOperation(operation_, noun_);
}

void OperationBanner::setOperation(git::Operation operation, const QString& noun) {
    operation_ = operation;
    noun_ = noun;
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
    if (!editing_.isEmpty()) {
        text_->setText(tr("Stopped to edit \"%1\". Its changes are staged in Local Changes: "
                          "change, stage or unstage them, then Continue — it recommits them "
                          "with the message in the commit box and carries on with the rebase.")
                           .arg(editing_));
        setVisible(true);
        return;
    }
    text_->setText(tr("You are in the middle of %1. Resolve any conflicts and stage the "
                      "results, then continue — or abandon it to return to where you were.")
                       .arg(noun));
    setVisible(true);
}

} // namespace gity::ui
