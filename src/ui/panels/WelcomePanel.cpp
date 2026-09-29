#include "WelcomePanel.h"

#include "ui/panels/Confirm.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace gity::ui {

WelcomePanel::WelcomePanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("welcomePanel"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto* outer = new QHBoxLayout(this);
    outer->addStretch(1);
    auto* column = new QVBoxLayout;
    column->setSpacing(10);
    outer->addLayout(column, 2);
    outer->addStretch(1);

    column->addStretch(1);
    auto* title = new QLabel(tr("Gity"), this);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 2.0);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    column->addWidget(title);

    auto* actions = new QHBoxLayout;
    auto* open = new QPushButton(tr("Open Repository…"), this);
    auto* clone = new QPushButton(tr("Clone Repository…"), this);
    applyRole(open, QStringLiteral("primary"));
    actions->addWidget(open);
    actions->addWidget(clone);
    actions->addStretch(1);
    column->addLayout(actions);

    recentHeading_ = new QLabel(tr("Recent"), this);
    applyRole(recentHeading_, QStringLiteral("note"));
    column->addSpacing(12);
    column->addWidget(recentHeading_);

    recent_ = new QListWidget(this);
    recent_->setObjectName(QStringLiteral("welcomeRecent"));
    recent_->setMinimumHeight(220);
    column->addWidget(recent_, 3);
    column->addStretch(1);

    connect(open, &QPushButton::clicked, this, &WelcomePanel::openRequested);
    connect(clone, &QPushButton::clicked, this, &WelcomePanel::cloneRequested);
    // A single click: this is a launcher, and every row is an action.
    connect(recent_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if ((item->flags() & Qt::ItemIsEnabled) != 0) {
            emit recentActivated(item->data(Qt::UserRole).toString());
        }
    });
}

void WelcomePanel::setRecent(const QStringList& paths) {
    recent_->clear();
    for (const QString& path : paths) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1    %2").arg(QDir(path).dirName(), QDir::toNativeSeparators(path)),
            recent_);
        item->setData(Qt::UserRole, path);
        item->setToolTip(QDir::toNativeSeparators(path));
        if (!QFileInfo::exists(path)) {
            // Said in words as well as by greying: a dimmed row alone reads
            // as a styling accident.
            item->setText(tr("%1    (not found)").arg(item->text()));
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            item->setToolTip(tr("%1 — no longer on disk").arg(QDir::toNativeSeparators(path)));
        }
    }
    recentHeading_->setVisible(!paths.isEmpty());
    recent_->setVisible(!paths.isEmpty());
}

} // namespace gity::ui
