#include "PreviewDialog.h"

#include "ui/panels/Confirm.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace gity::ui {

PreviewDialog::PreviewDialog(const QString& title, QWidget* parent) : QDialog(parent) {
    setWindowTitle(title);
    setModal(true);
    setMinimumWidth(560);

    auto* layout = new QVBoxLayout(this);
    body_ = new QVBoxLayout;
    body_->setSpacing(8);
    layout->addLayout(body_);
    layout->addStretch(1);

    buttons_ = new QHBoxLayout;
    buttons_->addStretch(1);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons_->addWidget(cancel);
    layout->addLayout(buttons_);
}

void PreviewDialog::setHeadline(const QString& text) {
    auto* label = new QLabel(text, this);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    QFont font = label->font();
    font.setWeight(QFont::DemiBold);
    font.setPointSizeF(font.pointSizeF() * 1.1);
    label->setFont(font);
    body_->insertWidget(0, label);
}

void PreviewDialog::addText(const QString& text, const QString& role) {
    auto* label = new QLabel(text, this);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    if (!role.isEmpty()) {
        applyRole(label, role);
    }
    body_->addWidget(label);
}

void PreviewDialog::addList(const QString& heading, const QStringList& items, int total,
                            const QString& role) {
    if (items.isEmpty()) {
        return;
    }
    auto* label = new QLabel(heading, this);
    label->setTextFormat(Qt::PlainText);
    if (!role.isEmpty()) {
        applyRole(label, role);
    }
    body_->addWidget(label);

    auto* list = new QListWidget(this);
    list->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    list->addItems(items);
    if (total > items.size()) {
        list->addItem(tr("… and %L1 more").arg(total - items.size()));
    }
    list->setSelectionMode(QAbstractItemView::NoSelection);
    list->setFocusPolicy(Qt::NoFocus);
    const int rows = std::min(8, list->count());
    list->setFixedHeight(list->sizeHintForRow(0) * rows + 2 * list->frameWidth() + 6);
    body_->addWidget(list);
    body_->addSpacing(4);
}

void PreviewDialog::setCommand(const QString& command) {
    command_ = command;
}

void PreviewDialog::addChoice(const QString& label, int id, const QString& role) {
    auto* button = new QPushButton(label, this);
    if (!role.isEmpty()) {
        applyRole(button, role);
    }
    if (role == QStringLiteral("primary") && !hasDefault_) {
        button->setDefault(true);
        hasDefault_ = true;
    }
    connect(button, &QPushButton::clicked, this, [this, id] {
        chosen_ = id;
        accept();
    });
    buttons_->addWidget(button);
}

int PreviewDialog::choose() {
    if (!command_.isEmpty()) {
        auto* command = new QLabel(QStringLiteral("git %1").arg(command_), this);
        command->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        command->setTextInteractionFlags(Qt::TextSelectableByMouse);
        // A lease names a whole ref and an id; wrapped, not cut off.
        command->setWordWrap(true);
        applyRole(command, QStringLiteral("note"));
        body_->addWidget(command);
    }
    chosen_ = -1;
    return exec() == QDialog::Accepted ? chosen_ : -1;
}

} // namespace gity::ui
