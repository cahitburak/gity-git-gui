#include "DiffView.h"

#include "ui/theme/Theme.h"
#include "ui/theme/Tokens.h"

#include <QFontDatabase>
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScrollBar>

#include <algorithm>

namespace gity::ui {
namespace {

constexpr int kLineNumberDigits = 5;
constexpr int kMarkerColumns = 2;

} // namespace

DiffView::DiffView(QWidget* parent) : QAbstractScrollArea(parent) {
    viewport()->setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    placeholder_ = tr("Select a file to see its diff");
    setAccessibleName(tr("Diff"));
    setAccessibleDescription(tr("When staging: Up and Down move between lines, Shift extends, "
                                "Space selects a line, Enter stages, Delete discards the "
                                "hunk."));
}

QFont DiffView::monoFont() const {
    // A diff is columnar: alignment carries meaning, so the platform's fixed
    // font is not a stylistic choice here.
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSizeF(QFontInfo(this->font()).pointSizeF());
    return font;
}

int DiffView::rowHeight() const {
    return QFontMetrics(monoFont()).height() + 2;
}

int DiffView::gutterWidth() const {
    const QFontMetrics fm(monoFont());
    const int digit = fm.horizontalAdvance(QChar('0'));
    // Staging narrows the number columns to make room for the pick gutter,
    // rather than pushing the code further right.
    const int digits = staging_ ? kLineNumberDigits - 1 : kLineNumberDigits;
    return (staging_ ? tokens::diffGutterStagePickWidth : 0) +
           digit * (digits * 2 + kMarkerColumns + 3);
}

void DiffView::setShowingStaged(bool staged) {
    showingStaged_ = staged;
    viewport()->update();
}

void DiffView::setSelectable(bool selectable) {
    selectable_ = selectable;
}

void DiffView::setStagingMode(bool staging) {
    staging_ = staging;
    updateScrollBars();
    viewport()->update();
}

void DiffView::clearSelection() {
    if (selection_.empty()) {
        return;
    }
    selection_.clear();
    anchorRow_ = -1;
    viewport()->update();
    emit selectionChanged(0);
}

qsizetype DiffView::rowAt(int viewportY) const {
    if (rows_.empty()) {
        return -1;
    }
    const qsizetype row = (verticalScrollBar()->value() + viewportY) / rowHeight();
    return (row >= 0 && row < static_cast<qsizetype>(rows_.size())) ? row : -1;
}

void DiffView::selectRowRange(qsizetype first, qsizetype last, bool additive) {
    if (!additive) {
        selection_.clear();
    }
    if (first > last) {
        std::swap(first, last);
    }
    for (qsizetype i = first; i <= last && i < static_cast<qsizetype>(rows_.size()); ++i) {
        const Row& row = rows_[static_cast<std::size_t>(i)];
        if (row.isHunkHeader) {
            // Clicking a hunk header selects the whole hunk — the common case,
            // and it saves dragging across a long block.
            const git::DiffHunk& hunk = diff_->hunks[row.index];
            for (std::uint32_t l = 0; l < hunk.lineCount; ++l) {
                const std::uint32_t lineIndex = hunk.firstLine + l;
                const auto kind = diff_->lines[lineIndex].kind;
                if (kind == git::DiffLineKind::Addition || kind == git::DiffLineKind::Deletion) {
                    selection_.insert(lineIndex);
                }
            }
            continue;
        }
        // Only changed lines are stageable; selecting context would be a no-op
        // that looks like it did something.
        const auto kind = diff_->lines[row.index].kind;
        if (kind == git::DiffLineKind::Addition || kind == git::DiffLineKind::Deletion) {
            selection_.insert(row.index);
        }
    }
    viewport()->update();
    emit selectionChanged(static_cast<int>(selection_.size()));
}

void DiffView::mousePressEvent(QMouseEvent* event) {
    if (!selectable_ || !diff_) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    const qsizetype row = rowAt(static_cast<int>(event->position().y()));
    if (row < 0) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    if (staging_) {
        const Row& hit = rows_[static_cast<std::size_t>(row)];
        const QPoint at(static_cast<int>(event->position().x()),
                        static_cast<int>(event->position().y()));

        if (hit.isHunkHeader && hit.index < hunkButtons_.size()) {
            if (hunkButtons_[hit.index].stage.contains(at)) {
                emit hunkStageRequested(hit.index);
                return;
            }
            if (!showingStaged_ && hunkButtons_[hit.index].discard.contains(at)) {
                emit hunkDiscardRequested(hit.index);
                return;
            }
        }

        // A click in the pick gutter acts immediately. It is the affordance
        // the spec puts there, and making it merely select would leave the
        // dot looking like a control that does nothing.
        if (!hit.isHunkHeader && at.x() < tokens::diffGutterStagePickWidth) {
            const auto kind = diff_->lines[hit.index].kind;
            if (kind == git::DiffLineKind::Addition || kind == git::DiffLineKind::Deletion) {
                emit lineToggled(hit.index);
                return;
            }
        }
    }

    cursorRow_ = row;
    const bool extend = (event->modifiers() & Qt::ShiftModifier) != 0 && anchorRow_ >= 0;
    const bool additive = (event->modifiers() & Qt::ControlModifier) != 0;
    if (extend) {
        selectRowRange(anchorRow_, row, additive);
    } else {
        anchorRow_ = row;
        selectRowRange(row, row, additive);
    }
    setFocus();
}

std::size_t DiffView::hunkAt(qsizetype row) const {
    for (qsizetype r = row; r >= 0; --r) {
        const Row& candidate = rows_[static_cast<std::size_t>(r)];
        if (candidate.isHunkHeader) {
            return candidate.index;
        }
    }
    return 0;
}

void DiffView::moveCursor(qsizetype row, bool extend) {
    if (rows_.empty()) {
        return;
    }
    row = std::clamp<qsizetype>(row, 0, static_cast<qsizetype>(rows_.size()) - 1);
    if (extend) {
        if (anchorRow_ < 0) {
            anchorRow_ = cursorRow_ >= 0 ? cursorRow_ : row;
        }
        selectRowRange(anchorRow_, row, false);
    } else {
        anchorRow_ = row;
    }
    cursorRow_ = row;

    const int h = rowHeight();
    const int top = static_cast<int>(row) * h;
    QScrollBar* bar = verticalScrollBar();
    if (top < bar->value()) {
        bar->setValue(top);
    } else if (top + h > bar->value() + viewport()->height()) {
        bar->setValue(top + h - viewport()->height());
    }
    viewport()->update();
}

void DiffView::keyPressEvent(QKeyEvent* event) {
    // Staging from the keyboard (2026-09 UI review): arrows move a line
    // cursor, Shift extends the selection, Space selects the line under it,
    // Enter stages, Delete discards the hunk. Everything the mouse can do to
    // lines and hunks, the keyboard can too.
    if (!selectable_ || !diff_ || rows_.empty()) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }
    const bool shift = (event->modifiers() & Qt::ShiftModifier) != 0;
    const qsizetype page = std::max<qsizetype>(1, viewport()->height() / rowHeight() - 1);
    const qsizetype at = std::max<qsizetype>(0, cursorRow_);
    switch (event->key()) {
    case Qt::Key_Escape:
        clearSelection();
        return;
    case Qt::Key_Down:
        moveCursor(cursorRow_ < 0 ? 0 : at + 1, shift);
        return;
    case Qt::Key_Up:
        moveCursor(at - 1, shift);
        return;
    case Qt::Key_PageDown:
        moveCursor(at + page, shift);
        return;
    case Qt::Key_PageUp:
        moveCursor(at - page, shift);
        return;
    case Qt::Key_Home:
        moveCursor(0, shift);
        return;
    case Qt::Key_End:
        moveCursor(static_cast<qsizetype>(rows_.size()) - 1, shift);
        return;
    case Qt::Key_Space: {
        if (cursorRow_ < 0) {
            moveCursor(0, false);
        }
        const Row& row = rows_[static_cast<std::size_t>(cursorRow_)];
        // A selected line toggles off; anything else is added, a header
        // taking its whole hunk.
        if (!row.isHunkHeader && selection_.count(row.index) != 0) {
            selection_.erase(row.index);
            viewport()->update();
            emit selectionChanged(static_cast<int>(selection_.size()));
        } else {
            selectRowRange(cursorRow_, cursorRow_, true);
        }
        anchorRow_ = cursorRow_;
        return;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!selection_.empty()) {
            emit stageSelectionRequested();
        } else if (cursorRow_ >= 0 && staging_) {
            emit hunkStageRequested(hunkAt(cursorRow_));
        }
        return;
    case Qt::Key_Delete:
        if (cursorRow_ >= 0 && staging_ && !showingStaged_) {
            emit hunkDiscardRequested(hunkAt(cursorRow_));
        }
        return;
    default:
        break;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void DiffView::setDiff(FileDiffPtr diff, session::HunkStagingPtr staging) {
    hunkStaging_ = std::move(staging);
    diff_ = std::move(diff);
    selection_.clear();
    anchorRow_ = -1;
    cursorRow_ = -1;
    emit selectionChanged(0);
    rebuildRows();
    verticalScrollBar()->setValue(0);
    horizontalScrollBar()->setValue(0);
    viewport()->update();
}

void DiffView::clearDiff() {
    diff_.reset();
    selection_.clear();
    anchorRow_ = -1;
    cursorRow_ = -1;
    emit selectionChanged(0);
    rows_.clear();
    longestLine_ = 0;
    updateScrollBars();
    viewport()->update();
}

void DiffView::setPlaceholder(const QString& text) {
    placeholder_ = text;
    viewport()->update();
}

void DiffView::rebuildRows() {
    rows_.clear();
    hunkButtons_.clear();
    longestLine_ = 0;
    if (!diff_) {
        updateScrollBars();
        return;
    }

    rows_.reserve(diff_->lines.size() + diff_->hunks.size());
    for (std::uint32_t h = 0; h < diff_->hunks.size(); ++h) {
        const git::DiffHunk& hunk = diff_->hunks[h];
        rows_.push_back(Row{true, h});
        for (std::uint32_t l = 0; l < hunk.lineCount; ++l) {
            const std::uint32_t lineIndex = hunk.firstLine + l;
            rows_.push_back(Row{false, lineIndex});
            longestLine_ = std::max<int>(
                longestLine_, static_cast<int>(diff_->lines[lineIndex].textLength));
        }
    }
    updateScrollBars();
}

void DiffView::updateScrollBars() {
    const int h = rowHeight();
    const int totalHeight = static_cast<int>(rows_.size()) * h;
    const int page = viewport()->height();
    verticalScrollBar()->setRange(0, std::max(0, totalHeight - page));
    verticalScrollBar()->setPageStep(page);
    verticalScrollBar()->setSingleStep(h);

    const QFontMetrics fm(monoFont());
    const int charWidth = std::max(1, fm.horizontalAdvance(QChar('0')));
    const int totalWidth = gutterWidth() + (longestLine_ + 2) * charWidth;
    const int pageWidth = viewport()->width();
    horizontalScrollBar()->setRange(0, std::max(0, totalWidth - pageWidth));
    horizontalScrollBar()->setPageStep(pageWidth);
    horizontalScrollBar()->setSingleStep(charWidth * 4);
}

void DiffView::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

void DiffView::paintEvent(QPaintEvent* event) {
    const Theme& theme = Theme::current();
    QPainter painter(viewport());
    painter.fillRect(event->rect(), theme.background);

    if (!diff_ || rows_.empty()) {
        painter.setPen(theme.textFaint);
        QString message = placeholder_;
        if (diff_ && diff_->binary) {
            message = tr("Binary file — no textual diff");
        } else if (diff_ && diff_->lines.empty()) {
            message = tr("No textual changes");
        }
        painter.drawText(viewport()->rect(), Qt::AlignCenter, message);
        paintFocus(painter);
        return;
    }

    painter.setFont(monoFont());

    const int h = rowHeight();
    const int scroll = verticalScrollBar()->value();
    const int gutter = gutterWidth();

    const auto first = static_cast<std::size_t>(std::max(0, (scroll + event->rect().top()) / h));
    const auto last = std::min<std::size_t>(
        rows_.size() - 1, static_cast<std::size_t>((scroll + event->rect().bottom()) / h));

    // The gutter is painted as one band behind the rows so line-number
    // alignment does not depend on every row repainting it identically.
    painter.fillRect(0, 0, gutter, viewport()->height(), theme.gutterBg);

    for (std::size_t i = first; i <= last && i < rows_.size(); ++i) {
        paintRow(painter, rows_[i], static_cast<int>(i) * h - scroll, gutter);
    }

    // The keyboard's line, outlined — distinct from the selection's fill.
    if (hasFocus() && cursorRow_ >= 0 && cursorRow_ < static_cast<qsizetype>(rows_.size())) {
        painter.setPen(QPen(tokens::selectionMarker, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRect(0, static_cast<int>(cursorRow_) * h - scroll,
                               viewport()->width() - 1, h - 1));
    }

    painter.setPen(theme.separator);
    painter.drawLine(gutter, 0, gutter, viewport()->height());
    paintFocus(painter);
}

void DiffView::paintRow(QPainter& painter, const Row& row, int y, int gutter) const {
    const Theme& theme = Theme::current();
    const int h = rowHeight();
    const int width = viewport()->width();
    const QFontMetrics fm(monoFont());
    const int digit = fm.horizontalAdvance(QChar('0'));
    const int xScroll = horizontalScrollBar()->value();

    if (row.isHunkHeader) {
        const git::DiffHunk& hunk = diff_->hunks[row.index];
        painter.fillRect(0, y, width, h, theme.hunkHeaderBg);
        painter.setPen(theme.hunkHeaderText);
        const QString headerText = QString::fromStdString(hunk.header);
        painter.drawText(QRect(digit, y, width - digit, h), Qt::AlignVCenter | Qt::AlignLeft,
                         headerText);

        // SPEC.md §2: "@@ -12,6 +12,9 @@  ·  2 of 4 lines staged". Shown only
        // when the region really is split across the index — "0 of 4" on a
        // file nobody has staged anything from is noise on every hunk.
        if (hunkStaging_ != nullptr && row.index < hunkStaging_->size() &&
            hunkStaging_->at(row.index).partial()) {
            const git::HunkStaging& counts = hunkStaging_->at(row.index);
            const QString note = tr("  ·  %1 of %2 lines staged")
                                     .arg(counts.staged)
                                     .arg(counts.total);
            painter.setPen(tokens::textLabelDim);
            painter.drawText(QRect(digit + fm.horizontalAdvance(headerText), y,
                                   width - digit, h),
                             Qt::AlignVCenter | Qt::AlignLeft, note);
        }

        if (staging_) {
            // Painted, not real buttons: child widgets inside a scrolling view
            // are the usual cause of jank here (QT_MAPPING).
            if (hunkButtons_.size() <= row.index) {
                hunkButtons_.resize(row.index + 1);
            }
            const QFontMetrics buttonMetrics(font());
            const auto paintButton = [&](int right, const QString& label,
                                         const QColor& colour) {
                const int buttonWidth = buttonMetrics.horizontalAdvance(label) + 16;
                const QRect box(right - buttonWidth, y + (h - buttonMetrics.height() - 4) / 2,
                                buttonWidth, buttonMetrics.height() + 4);
                painter.setFont(font());
                painter.setPen(QPen(colour, 1));
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(box, tokens::radiusChip, tokens::radiusChip);
                painter.drawText(box, Qt::AlignCenter, label);
                return box;
            };
            if (showingStaged_) {
                const QRect unstage =
                    paintButton(width - 8, tr("Unstage hunk"), tokens::semanticWarn);
                hunkButtons_[row.index] = {unstage, QRect()};
            } else {
                const QRect discard =
                    paintButton(width - 8, tr("Discard"), tokens::semanticRemove);
                const QRect stage =
                    paintButton(discard.left() - 6, tr("Stage hunk"), tokens::semanticAdd);
                hunkButtons_[row.index] = {stage, discard};
            }
            painter.setFont(monoFont());
        }
        return;
    }

    const git::DiffLine& line = diff_->lines[row.index];

    QColor background = theme.background;
    QColor marker = theme.textFaint;
    QChar markerChar(' ');
    switch (line.kind) {
    case git::DiffLineKind::Addition:
        background = theme.diffAddBg;
        marker = theme.diffAddMarker;
        markerChar = QChar('+');
        break;
    case git::DiffLineKind::Deletion:
        background = theme.diffDelBg;
        marker = theme.diffDelMarker;
        markerChar = QChar('-');
        break;
    case git::DiffLineKind::NoNewlineMarker:
        marker = theme.textFaint;
        markerChar = QChar('\\');
        break;
    case git::DiffLineKind::Context:
        break;
    }

    if (background != theme.background) {
        painter.fillRect(0, y, width, h, background);
    }
    if (selectable_ && selection_.count(row.index) != 0) {
        painter.fillRect(0, y, width, h, theme.selection);
    }

    // The pick gutter: a dot on every stageable line, nothing on context. It
    // is the click target, so an empty cell means "nothing to do here".
    if (staging_) {
        if (line.kind == git::DiffLineKind::Addition ||
            line.kind == git::DiffLineKind::Deletion) {
            painter.setPen(selection_.count(row.index) != 0 ? theme.diffAddMarker
                                                            : tokens::surfacePickDot);
            painter.drawText(QRect(0, y, tokens::diffGutterStagePickWidth, h),
                             Qt::AlignCenter, QStringLiteral("·"));
        }
    }

    // Line numbers: blank on the side a line does not exist on, which is what
    // makes an addition and a deletion distinguishable at a glance without
    // relying on colour alone.
    painter.setPen(theme.gutterText);
    const int pickOffset = staging_ ? tokens::diffGutterStagePickWidth : 0;
    const int numberWidth = digit * (staging_ ? kLineNumberDigits - 1 : kLineNumberDigits);
    if (line.oldLine > 0) {
        painter.drawText(QRect(pickOffset + digit, y, numberWidth, h),
                         Qt::AlignVCenter | Qt::AlignRight, QString::number(line.oldLine));
    }
    if (line.newLine > 0) {
        painter.drawText(QRect(pickOffset + digit * 2 + numberWidth, y, numberWidth, h),
                         Qt::AlignVCenter | Qt::AlignRight, QString::number(line.newLine));
    }

    painter.setPen(marker);
    painter.drawText(QRect(gutter - digit * 2, y, digit * 2, h), Qt::AlignVCenter | Qt::AlignLeft,
                     QString(markerChar));

    painter.setPen(theme.text);
    // Only as much of the line as can reach the viewport. A minified file is
    // one line of a megabyte, and converting and laying out all of it for
    // every repaint of every row made scrolling past it crawl. Bytes are an
    // over-estimate of characters, so this never cuts short what is visible.
    auto text = diff_->lineText(line);
    const std::size_t reach =
        static_cast<std::size_t>((xScroll + width) / std::max(1, digit)) + 64;
    if (text.size() > reach) {
        text = text.substr(0, reach);
    }
    painter.drawText(QRect(gutter + digit - xScroll, y, xScroll + width, h),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())));
}

void DiffView::focusInEvent(QFocusEvent* event) {
    QAbstractScrollArea::focusInEvent(event);
    viewport()->update();
}

void DiffView::focusOutEvent(QFocusEvent* event) {
    QAbstractScrollArea::focusOutEvent(event);
    viewport()->update();
}

void DiffView::paintFocus(QPainter& painter) const {
    // The keyboard is here: one accent line round the view, the same marker
    // the stylesheet gives focused lists and fields.
    if (hasFocus()) {
        painter.setPen(QPen(tokens::selectionMarker, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(viewport()->rect().adjusted(0, 0, -1, -1));
    }
}

} // namespace gity::ui
