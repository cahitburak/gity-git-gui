#include "BlamePanel.h"

#include "Confirm.h"
#include "ui/diffview/BlameView.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

namespace gity::ui {

BlamePanel::BlamePanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 4, 12, 4);
    back_ = new QToolButton(header);
    back_->setText(tr("‹ Back"));
    back_->setToolTip(tr("Return to the version blamed before this one."));
    back_->setAutoRaise(true);
    title_ = new QLabel(header);
    title_->setTextFormat(Qt::PlainText);
    QFont bold = title_->font();
    bold.setBold(true);
    title_->setFont(bold);
    meta_ = new QLabel(header);
    meta_->setTextFormat(Qt::PlainText);
    applyRole(meta_, QStringLiteral("muted"));
    headerLayout->addWidget(back_);
    headerLayout->addWidget(title_);
    headerLayout->addWidget(meta_, 1);
    layout->addWidget(header);

    view_ = new BlameView(this);
    layout->addWidget(view_, 1);

    connect(back_, &QToolButton::clicked, this, [this] {
        if (trail_.empty()) {
            return;
        }
        const Place previous = trail_.back();
        trail_.pop_back();
        go(previous);
    });
    connect(view_, &BlameView::commitActivated, this, &BlamePanel::commitActivated);
    connect(view_, &BlameView::previousRequested, this,
            [this](const QString& previousId, const QString& previousPath) {
                trail_.push_back(current_);
                go({previousPath, previousId});
            });
    clear();
}

void BlamePanel::start(const QString& path, const QString& revision) {
    trail_.clear();
    go({path, revision});
}

void BlamePanel::go(const Place& place) {
    current_ = place;
    back_->setVisible(!trail_.empty());
    title_->setText(place.path);
    view_->clearBlame();
    view_->setPlaceholder(tr("Reading who changed each line…"));
    describe(tr("loading…"));
    emit blameRequested(place.path, place.revision);
}

void BlamePanel::describe(const QString& state) {
    const QString where = current_.revision.isEmpty()
                              ? tr("as it is in the working copy, edits included")
                              : tr("as of %1").arg(current_.revision.left(7));
    meta_->setText(state.isEmpty() ? where : QStringLiteral("%1  ·  %2").arg(where, state));
}

void BlamePanel::setBlame(const QString& path, const QString& revision,
                          const session::BlamePtr& blame, const QString& problem) {
    if (path != current_.path || revision != current_.revision) {
        return;
    }
    if (!blame) {
        view_->clearBlame();
        view_->setPlaceholder(problem.isEmpty()
                                  ? tr("git could not blame this file.")
                                  : tr("git could not blame this file: %1").arg(problem));
        describe({});
        return;
    }
    view_->setBlame(blame);
    describe(tr("%n line(s), from %1", nullptr, static_cast<int>(blame->lines.size()))
                 .arg(tr("%n commit(s)", nullptr, static_cast<int>(blame->commits.size()))));
}

void BlamePanel::clear() {
    current_ = {};
    trail_.clear();
    back_->setVisible(false);
    title_->clear();
    meta_->clear();
    view_->clearBlame();
    view_->setPlaceholder(
        tr("Right-click a file and choose Blame to see who last changed each line."));
}

} // namespace gity::ui
