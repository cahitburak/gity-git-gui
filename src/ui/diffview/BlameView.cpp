#include "BlameView.h"

#include "ui/theme/Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QFontDatabase>
#include <QHelpEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolTip>

#include <algorithm>

namespace gity::ui {
namespace {

/// The who-and-when column, in characters: a short id, an author and a date.
constexpr int kLabelChars = 38;
constexpr int kTabWidth = 4;

QString expandTabs(const std::string& line) {
    const QString text = QString::fromStdString(line);
    if (!text.contains(QChar('\t'))) {
        return text;
    }
    QString out;
    out.reserve(text.size() + 16);
    for (const QChar c : text) {
        if (c == QChar('\t')) {
            out += QString(kTabWidth - (out.size() % kTabWidth), QChar(' '));
        } else {
            out += c;
        }
    }
    return out;
}

} // namespace

BlameView::BlameView(QWidget* parent) : QAbstractScrollArea(parent) {
    setFrameShape(QFrame::NoFrame);
    viewport()->setMouseTracking(true);
    setPlaceholder(tr("Right-click a file and choose Blame to see who last changed each line."));
}

void BlameView::setBlame(session::BlamePtr blame) {
    blame_ = std::move(blame);
    labels_.clear();
    texts_.clear();
    runs_.clear();
    longestLine_ = 0;
    highlighted_ = -1;
    if (blame_) {
        labels_.reserve(blame_->commits.size());
        for (const auto& commit : blame_->commits) {
            Label label;
            label.uncommitted = commit.uncommitted();
            const QString author = QString::fromStdString(commit.author);
            const QDateTime when = QDateTime::fromSecsSinceEpoch(commit.when);
            if (label.uncommitted) {
                label.author = tr("Not committed yet");
                label.tooltip = tr("Changed in the working copy since the last commit.");
            } else {
                label.shortId = QString::fromStdString(commit.id).left(7);
                label.author = author;
                label.date = when.toString(QStringLiteral("yyyy-MM-dd"));
                label.tooltip =
                    QStringLiteral("%1\n%2  ·  %3  ·  %4")
                        .arg(QString::fromStdString(commit.summary), author,
                             when.toString(QStringLiteral("yyyy-MM-dd hh:mm")), label.shortId);
            }
            labels_.push_back(std::move(label));
        }
        texts_.reserve(blame_->lines.size());
        runs_.reserve(blame_->lines.size());
        std::uint32_t run = 0;
        for (std::size_t i = 0; i < blame_->lines.size(); ++i) {
            const auto& line = blame_->lines[i];
            if (i > 0 && line.commit != blame_->lines[i - 1].commit) {
                ++run;
            }
            runs_.push_back(run);
            texts_.push_back(expandTabs(line.text));
            longestLine_ = std::max(longestLine_, static_cast<int>(texts_.back().size()));
        }
    }
    verticalScrollBar()->setValue(0);
    horizontalScrollBar()->setValue(0);
    updateScrollBars();
    viewport()->update();
}

void BlameView::clearBlame() {
    setBlame(nullptr);
}

void BlameView::setPlaceholder(const QString& text) {
    placeholder_ = text;
    viewport()->update();
}

QFont BlameView::monoFont() const {
    if (!monoFontValid_) {
        monoFont_ = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        monoFont_.setPointSizeF(QFontInfo(this->font()).pointSizeF());
        monoFontValid_ = true;
    }
    return monoFont_;
}

int BlameView::rowHeight() const {
    return QFontMetrics(monoFont()).height() + 2;
}

int BlameView::labelWidth() const {
    return QFontMetrics(monoFont()).horizontalAdvance(QChar('0')) * kLabelChars + 16;
}

int BlameView::numberWidth() const {
    const int digits = blame_ ? static_cast<int>(QString::number(blame_->lines.size()).size()) : 1;
    return QFontMetrics(monoFont()).horizontalAdvance(QChar('0')) * (std::max(digits, 3) + 2);
}

void BlameView::changeEvent(QEvent* event) {
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
        monoFontValid_ = false;
        updateScrollBars();
    }
    QAbstractScrollArea::changeEvent(event);
}

void BlameView::updateScrollBars() {
    const int h = rowHeight();
    const int lines = blame_ ? static_cast<int>(blame_->lines.size()) : 0;
    const int page = viewport()->height();
    verticalScrollBar()->setRange(0, std::max(0, lines * h - page));
    verticalScrollBar()->setPageStep(page);
    verticalScrollBar()->setSingleStep(h);

    // Only the text scrolls sideways; who changed a line stays in view.
    const int charWidth = std::max(1, QFontMetrics(monoFont()).horizontalAdvance(QChar('0')));
    const int textRoom = viewport()->width() - labelWidth() - numberWidth();
    horizontalScrollBar()->setRange(0, std::max(0, (longestLine_ + 2) * charWidth - textRoom));
    horizontalScrollBar()->setPageStep(std::max(1, textRoom));
    horizontalScrollBar()->setSingleStep(charWidth * 4);
}

void BlameView::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

qsizetype BlameView::rowAt(int viewportY) const {
    if (!blame_) {
        return -1;
    }
    const int row = (viewportY + verticalScrollBar()->value()) / rowHeight();
    return row >= 0 && row < static_cast<int>(blame_->lines.size()) ? row : -1;
}

void BlameView::paintEvent(QPaintEvent* event) {
    const Theme& theme = Theme::current();
    QPainter painter(viewport());
    painter.fillRect(event->rect(), theme.background);

    if (!blame_ || blame_->lines.empty()) {
        painter.setPen(theme.textFaint);
        painter.drawText(viewport()->rect().adjusted(24, 0, -24, 0),
                         Qt::AlignCenter | Qt::TextWordWrap,
                         blame_ ? tr("This file is empty") : placeholder_);
        return;
    }

    painter.setFont(monoFont());
    const QFontMetrics fm(monoFont());
    const int h = rowHeight();
    const int scroll = verticalScrollBar()->value();
    const int label = labelWidth();
    const int numbers = numberWidth();
    const int textLeft = label + numbers;
    const int width = viewport()->width();
    const auto count = static_cast<qsizetype>(blame_->lines.size());
    const qsizetype first = std::max<qsizetype>(0, (scroll + event->rect().top()) / h);
    const qsizetype last = std::min<qsizetype>(count - 1, (scroll + event->rect().bottom()) / h);

    painter.fillRect(0, 0, textLeft, viewport()->height(), theme.gutterBg);

    for (qsizetype i = first; i <= last; ++i) {
        const auto index = static_cast<std::size_t>(i);
        const auto& line = blame_->lines[index];
        const Label& who = labels_[line.commit];
        const int y = static_cast<int>(i) * h - scroll;
        const bool runStart = i == 0 || runs_[index] != runs_[index - 1];

        // Every line of the picked commit stands out, wherever it is.
        QColor band;
        if (static_cast<qsizetype>(line.commit) == highlighted_) {
            band = theme.hover;
        } else if (who.uncommitted) {
            band = theme.diffAddBg;
        } else if (runs_[index] % 2 == 1) {
            band = theme.rowAlternate;
        }
        if (band.isValid()) {
            painter.fillRect(0, y, width, h, band);
        }
        if (runStart && i > 0) {
            painter.setPen(theme.separator);
            painter.drawLine(0, y, width, y);
        }

        // The commit is named at the top of its run, and again at the top of
        // the view when a run starts above it, so no visible line is
        // anonymous.
        if (runStart || i == first) {
            const int baseline = y + (h + fm.ascent() - fm.descent()) / 2;
            int x = 8;
            if (who.uncommitted) {
                painter.setPen(theme.diffAddMarker);
                painter.drawText(x, baseline,
                                 fm.elidedText(who.author, Qt::ElideRight, label - 16));
            } else {
                painter.setPen(theme.textMuted);
                painter.drawText(x, baseline, who.shortId);
                x += fm.horizontalAdvance(QChar('0')) * 8;
                const int dateWidth = fm.horizontalAdvance(who.date);
                const int dateX = label - 8 - dateWidth;
                painter.setPen(theme.text);
                painter.drawText(x, baseline,
                                 fm.elidedText(who.author, Qt::ElideRight, dateX - x - 8));
                painter.setPen(theme.textFaint);
                painter.drawText(dateX, baseline, who.date);
            }
        }

        painter.setPen(theme.gutterText);
        painter.drawText(QRect(label, y, numbers - 10, h), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(i + 1));

        painter.save();
        painter.setClipRect(textLeft, y, width - textLeft, h);
        painter.setPen(theme.text);
        painter.drawText(QRect(textLeft + 6 - horizontalScrollBar()->value(), y,
                               fm.horizontalAdvance(texts_[index]) + 8, h),
                         Qt::AlignLeft | Qt::AlignVCenter, texts_[index]);
        painter.restore();
    }

    painter.setPen(theme.separator);
    painter.drawLine(label, 0, label, viewport()->height());
    painter.drawLine(textLeft - 1, 0, textLeft - 1, viewport()->height());
}

void BlameView::mousePressEvent(QMouseEvent* event) {
    const qsizetype row = rowAt(event->position().toPoint().y());
    if (row < 0 || event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    const auto& line = blame_->lines[static_cast<std::size_t>(row)];
    highlighted_ = line.commit;
    viewport()->update();
    if (!labels_[line.commit].uncommitted) {
        emit commitActivated(QString::fromStdString(blame_->commits[line.commit].id));
    }
}

void BlameView::contextMenuEvent(QContextMenuEvent* event) {
    const qsizetype row = rowAt(viewport()->mapFrom(this, event->pos()).y());
    if (row < 0) {
        return;
    }
    const auto& line = blame_->lines[static_cast<std::size_t>(row)];
    const auto& commit = blame_->commits[line.commit];
    const Label& who = labels_[line.commit];
    highlighted_ = line.commit;
    viewport()->update();

    QMenu menu(this);
    QAction* show = menu.addAction(tr("Show Commit %1").arg(who.shortId));
    QAction* before = menu.addAction(tr("Blame Before This Change"));
    QAction* copy = menu.addAction(tr("Copy Commit Id"));
    show->setEnabled(!who.uncommitted);
    copy->setEnabled(!who.uncommitted);
    // Nothing before it: the line arrived with the file, or the history is
    // shallow. Lines not committed yet are blamed against HEAD by Blame on
    // the committed file instead.
    before->setEnabled(!commit.previousId.empty());

    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == show) {
        emit commitActivated(QString::fromStdString(commit.id));
    } else if (chosen == before) {
        emit previousRequested(QString::fromStdString(commit.previousId),
                               QString::fromStdString(commit.previousPath));
    } else if (chosen == copy) {
        QApplication::clipboard()->setText(QString::fromStdString(commit.id));
    }
}

bool BlameView::viewportEvent(QEvent* event) {
    if (event->type() == QEvent::ToolTip && blame_) {
        auto* help = static_cast<QHelpEvent*>(event);
        const qsizetype row = rowAt(help->pos().y());
        if (row >= 0 && help->pos().x() < labelWidth()) {
            const auto& line = blame_->lines[static_cast<std::size_t>(row)];
            QToolTip::showText(help->globalPos(), labels_[line.commit].tooltip, viewport());
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QAbstractScrollArea::viewportEvent(event);
}

} // namespace gity::ui
