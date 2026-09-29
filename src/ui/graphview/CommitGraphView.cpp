#include "CommitGraphView.h"

#include "GraphColumns.h"
#include "ui/theme/Theme.h"
#include "ui/theme/Tokens.h"

#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>

#include <algorithm>

namespace gity::ui {
namespace {

constexpr int kMaxDrawnLanes = 24; ///< Past this the gutter is clamped; M0 saw 14.

/// SPEC.md § Ref chips. Returns the width consumed so the caller can lay the
/// next chip beside it.
int paintRefChip(QPainter& painter, int x, int y, int height, const RefLabel& label,
                 int maxWidth) {
    QColor text = tokens::textDim;
    QColor fill = tokens::fillNeutralChip;
    QColor border = tokens::fillNeutralChipBorder;

    if (label.isHead) {
        text = tokens::semanticWarn;
        fill = tokens::fillWarnChip;
        border = tokens::fillWarnChipBorder;
    } else {
        switch (label.kind) {
        case git::RefKind::LocalBranch:
            text = tokens::accentPrimaryLight;
            fill = tokens::fillInfoChip;
            border = tokens::fillInfoChipBorder;
            break;
        case git::RefKind::Tag:
            text = tokens::semanticMeta;
            fill = tokens::fillMetaChip;
            border = tokens::fillMetaChipBorder;
            break;
        case git::RefKind::RemoteBranch:
        case git::RefKind::Stash:
            break; // neutral, set above
        }
    }

    QFont chipFont = painter.font();
    chipFont.setPointSize(std::max(7, chipFont.pointSize() - 1));
    chipFont.setWeight(QFont::Medium);
    const QFontMetrics fm(chipFont);

    constexpr int padX = 6;
    const int available = std::max(0, maxWidth - padX * 2);
    const QString shown = fm.elidedText(label.text, Qt::ElideRight, available);
    const int width = fm.horizontalAdvance(shown) + padX * 2;
    const int chipHeight = fm.height() + 3;
    const int top = y + (height - chipHeight) / 2;

    painter.setFont(chipFont);
    painter.setPen(QPen(border, 1.0));
    painter.setBrush(fill);
    painter.drawRoundedRect(QRectF(x + 0.5, top + 0.5, width - 1.0, chipHeight - 1.0),
                            tokens::radiusChip, tokens::radiusChip);

    painter.setPen(text);
    painter.drawText(QRect(x + padX, top, width - padX * 2, chipHeight),
                     Qt::AlignVCenter | Qt::AlignLeft, shown);
    return width;
}

/// A lane-changing edge, exactly as SPEC.md specifies it:
///     M x1 y1  C x1 (y1+13), x2 (y2-15), x2 y2
/// The control offsets are logical px at pitch 26 and scale with row height,
/// so dense rows keep the same curve shape rather than flattening.
QPainterPath laneCurve(int x1, int y1, int x2, int y2, int rowHeight) {
    const double scale = static_cast<double>(rowHeight) / tokens::rowHeight;
    QPainterPath path(QPointF(x1, y1));
    path.cubicTo(QPointF(x1, y1 + tokens::edgeControlDown * scale),
                 QPointF(x2, y2 + tokens::edgeControlUp * scale), QPointF(x2, y2));
    return path;
}

} // namespace

CommitGraphView::CommitGraphView(QWidget* parent) : QAbstractScrollArea(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    viewport()->setAutoFillBackground(false);
    viewport()->installEventFilter(this);
    // Named for assistive technology. The rows themselves are painted, so a
    // screen reader gets the view and its keys, not each commit — a full
    // QAccessible model for the rows is still to do (2026-09 UI review).
    setAccessibleName(tr("Commit history"));
    setAccessibleDescription(tr("Up and Down move between commits; Page Up, Page Down, Home "
                                "and End jump."));


    horizontalScrollBar()->setEnabled(false);
}

void CommitGraphView::setModel(HistoryModel* model) {
    model_ = model;
    selectedRow_ = -1;
    hoverRow_ = -1;
    updateScrollBars();
    viewport()->update();
}

int CommitGraphView::rowHeight() const {
    return Theme::current().metrics.rowHeight;
}

int CommitGraphView::laneX(int lane) const {
    const auto& m = Theme::current().metrics;
    return m.laneOriginX + lane * m.laneWidth;
}

int CommitGraphView::graphGutterWidth() const {
    const auto& m = Theme::current().metrics;
    const int lanes = model_ != nullptr ? std::min<int>(model_->maxLanes(), kMaxDrawnLanes) : 1;
    return m.laneOriginX + lanes * m.laneWidth + m.gutterPadding;
}

void CommitGraphView::rowsAppended() {
    updateScrollBars();
    viewport()->update();
}

void CommitGraphView::updateScrollBars() {
    const auto rows = static_cast<qsizetype>(model_ != nullptr ? model_->rowCount() : 0);
    const int total = static_cast<int>(std::min<qsizetype>(rows, INT_MAX / rowHeight())) *
                      rowHeight();
    const int page = viewport()->height();

    verticalScrollBar()->setRange(0, std::max(0, total - page));
    verticalScrollBar()->setPageStep(page);
    verticalScrollBar()->setSingleStep(rowHeight());
}

qsizetype CommitGraphView::rowAt(int viewportY) const {
    if (model_ == nullptr || model_->rowCount() == 0) {
        return -1;
    }
    const int absolute = verticalScrollBar()->value() + viewportY;
    const qsizetype row = absolute / rowHeight();
    return (row >= 0 && row < static_cast<qsizetype>(model_->rowCount())) ? row : -1;
}

void CommitGraphView::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

void CommitGraphView::paintEvent(QPaintEvent* event) {
    // Density changed since the last paint: the row height did, so the scroll
    // range and the selected row's position must follow.
    if (layoutGeneration_ != Theme::generation()) {
        layoutGeneration_ = Theme::generation();
        updateScrollBars();
        if (selectedRow_ >= 0) {
            scrollToRow(selectedRow_);
        }
    }
    const Theme& theme = Theme::current();
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(event->rect(), theme.background);

    if (model_ == nullptr || model_->rowCount() == 0) {
        painter.setPen(theme.textFaint);
        painter.drawText(viewport()->rect(), Qt::AlignCenter,
                         placeholder_.isEmpty() ? tr("No repository open — File ▸ Open "
                                                     "Repository…")
                                                : placeholder_);
        paintFocus(painter);
        return;
    }

    const int h = rowHeight();
    const int scroll = verticalScrollBar()->value();
    const auto rows = static_cast<qsizetype>(model_->rowCount());

    // Only the visible band is ever touched. This is the whole point of the
    // virtualized design: painting cost is independent of history length.
    const qsizetype first = std::max<qsizetype>(0, (scroll + event->rect().top()) / h);
    const qsizetype last =
        std::min<qsizetype>(rows - 1, (scroll + event->rect().bottom()) / h);

    for (qsizetype row = first; row <= last; ++row) {
        const int y = static_cast<int>(row * h) - scroll;
        paintRow(painter, row, y);
    }
    paintFocus(painter);
}

void CommitGraphView::paintRow(QPainter& painter, qsizetype row, int y) const {
    const Theme& theme = Theme::current();
    const int h = rowHeight();
    const int width = viewport()->width();

    // --- row background ---
    if (row == selectedRow_) {
        painter.fillRect(0, y, width, h, theme.selection);
    } else if (row == hoverRow_) {
        painter.fillRect(0, y, width, h, theme.hover);
    } else if ((row % 2) != 0) {
        painter.fillRect(0, y, width, h, theme.rowAlternate);
    }

    const RowView view = model_->at(static_cast<std::size_t>(row));
    if (!view.valid()) {
        return;
    }

    // --- graph ---
    paintEdges(painter, view, y);

    const graph::GraphRow& record = view.row();
    const QColor laneColor = theme.laneColor(record.lane);
    const int cx = laneX(record.lane);
    const int cy = y + h / 2;

    // Node geometry is from the spec, not invented: a merge is a hollow ring
    // filled with the window surface, which is what makes it read as a ring
    // rather than a differently coloured dot.
    if ((record.flags & graph::RowFlag_Merge) != 0) {
        painter.setPen(QPen(laneColor, tokens::mergeNodeStrokeWidth));
        painter.setBrush(tokens::surfaceWindow);
        painter.drawEllipse(QPointF(cx, cy), tokens::mergeNodeRadius, tokens::mergeNodeRadius);
    } else {
        painter.setPen(QPen(laneColor, tokens::nodeStrokeWidth));
        painter.setBrush(laneColor);
        painter.drawEllipse(QPointF(cx, cy), tokens::nodeRadius, tokens::nodeRadius);
    }
    painter.setBrush(Qt::NoBrush);

    // --- columns -------------------------------------------------------
    // SPEC.md order: Graph · Description, Message, Author, Date, Commit.
    const QFontMetrics fm(font());
    const bool selected = (row == selectedRow_);
    const QColor primary = selected ? theme.selectionText : theme.text;
    const QColor secondary = selected ? tokens::textSelectedDim : theme.textFaint;

    const GraphColumns columns =
        GraphColumns::forWidth(width, model_ != nullptr ? model_->maxLanes() : 1);
    const auto index = static_cast<std::size_t>(row);

    // Ref chips sit inline, just before the subject (2026-09 UI review): the
    // subject starts right after the lanes instead of across a wide gap. They
    // take at most two fifths of the message column; the rest become "+N".
    const int pad = 8;
    int textX = columns.messageX() + pad;
    const int messageEnd = columns.messageX() + columns.messageWidth - pad;
    if (const std::vector<RefLabel>* refs = model_->refsFor(index); refs != nullptr) {
        const int budget = columns.messageWidth * 2 / 5;
        int used = 0;
        std::size_t shown = 0;
        for (const RefLabel& label : *refs) {
            const int remaining = budget - used - 4;
            if (remaining < 40) {
                break;
            }
            used += paintRefChip(painter, textX + used, y, h, label, remaining) + 4;
            ++shown;
        }
        if (shown < refs->size()) {
            RefLabel more;
            more.text = QStringLiteral("+%1").arg(refs->size() - shown);
            more.kind = git::RefKind::RemoteBranch; // neutral
            used += paintRefChip(painter, textX + used, y, h, more, 48) + 4;
        }
        textX += used;
        painter.setFont(font());
    }

    painter.setPen(primary);
    painter.drawText(QRect(textX, y, std::max(0, messageEnd - textX), h),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     fm.elidedText(model_->summary(index), Qt::ElideRight,
                                   std::max(0, messageEnd - textX)));

    // Secondary columns a narrow window has dropped (width 0) are skipped.
    painter.setPen(secondary);
    if (columns.authorWidth > 0) {
        painter.drawText(QRect(columns.authorX() + pad, y, columns.authorWidth - pad * 2, h),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         fm.elidedText(model_->author(index), Qt::ElideRight,
                                       columns.authorWidth - pad * 2));
    }
    if (columns.dateWidth > 0) {
        painter.drawText(QRect(columns.dateX() + pad, y, columns.dateWidth - pad * 2, h),
                         Qt::AlignVCenter | Qt::AlignRight, model_->when(index));
    }
    if (columns.commitWidth > 0) {
        painter.drawText(QRect(columns.commitX() + pad, y, columns.commitWidth - pad * 2, h),
                         Qt::AlignVCenter | Qt::AlignLeft, model_->shortId(index));
    }
}

void CommitGraphView::paintEdges(QPainter& painter, const RowView& view, int y) const {
    const Theme& theme = Theme::current();
    const int h = rowHeight();
    const int top = y;
    const int bottom = y + h;
    const int middle = y + h / 2;

    const graph::GraphRow& record = view.row();
    const auto& edges = view.chunk->graph.edges;

    for (std::uint16_t e = 0; e < record.edgeCount; ++e) {
        const graph::GraphEdge& edge = edges[record.edgeOffset + e];
        if (edge.from >= kMaxDrawnLanes || edge.to >= kMaxDrawnLanes) {
            continue;
        }

        const int xFrom = laneX(edge.from);
        const int xTo = laneX(edge.to);

        // SPEC.md: an edge takes the *parent's* lane colour. A parent is always
        // below its child, so that is the destination lane in both directions —
        // colouring an incoming edge by its source is the intuitive mistake, and
        // it makes a merge's two legs disagree about which branch they belong to.
        painter.setPen(QPen(theme.laneColor(edge.to), tokens::edgeStrokeWidth, Qt::SolidLine,
                            Qt::RoundCap));

        switch (edge.kind) {
        case graph::EdgeKind::PassThrough:
            painter.drawLine(xFrom, top, xFrom, bottom);
            break;

        case graph::EdgeKind::In:
            if (xFrom == xTo) {
                painter.drawLine(xFrom, top, xTo, middle);
            } else {
                painter.drawPath(laneCurve(xFrom, top, xTo, middle, h));
            }
            break;

        case graph::EdgeKind::Out:
            if (xFrom == xTo) {
                painter.drawLine(xFrom, middle, xTo, bottom);
            } else {
                painter.drawPath(laneCurve(xFrom, middle, xTo, bottom, h));
            }
            break;
        }
    }
}

void CommitGraphView::select(qsizetype row) {
    if (row == selectedRow_ || model_ == nullptr) {
        return;
    }
    selectedRow_ = row;
    viewport()->update();
    emit rowSelected(row);
}

void CommitGraphView::selectRow(qsizetype row) {
    if (model_ == nullptr || row < 0 || row >= static_cast<qsizetype>(model_->rowCount())) {
        return;
    }
    select(row);
    // Selected from elsewhere — a branch clicked in the sidebar — the row can
    // be thousands down, and a selection nobody can see looks like nothing
    // happened.
    scrollToRow(row);
}

void CommitGraphView::setPlaceholder(const QString& text) {
    placeholder_ = text;
    viewport()->update();
}

void CommitGraphView::clearSelection() {
    if (selectedRow_ < 0) {
        return;
    }
    selectedRow_ = -1;
    viewport()->update();
}

void CommitGraphView::scrollToRow(qsizetype row) {
    const int h = rowHeight();
    const int topOf = static_cast<int>(row * h);
    QScrollBar* bar = verticalScrollBar();
    if (topOf < bar->value()) {
        bar->setValue(topOf);
    } else if (topOf + h > bar->value() + viewport()->height()) {
        bar->setValue(topOf + h - viewport()->height());
    }
}

bool CommitGraphView::eventFilter(QObject* watched, QEvent* event) {
    if (watched == viewport() && event->type() == QEvent::ContextMenu) {
        auto* menuEvent = static_cast<QContextMenuEvent*>(event);
        const qsizetype row = rowAt(menuEvent->pos().y());
        if (row < 0) {
            return QAbstractScrollArea::eventFilter(watched, event);
        }
        // Select first, so the commit acted on is the one under the cursor and
        // never a stale selection.
        selectRow(row);
        emit rowContextMenuRequested(row, menuEvent->globalPos());
        return true;
    }
    return QAbstractScrollArea::eventFilter(watched, event);
}

void CommitGraphView::mousePressEvent(QMouseEvent* event) {
    const qsizetype row = rowAt(static_cast<int>(event->position().y()));
    if (row >= 0) {
        select(row);
    }
    QAbstractScrollArea::mousePressEvent(event);
}


void CommitGraphView::mouseMoveEvent(QMouseEvent* event) {
    const qsizetype row = rowAt(static_cast<int>(event->position().y()));
    if (row != hoverRow_) {
        hoverRow_ = row;
        viewport()->update();
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void CommitGraphView::leaveEvent(QEvent* event) {
    if (hoverRow_ != -1) {
        hoverRow_ = -1;
        viewport()->update();
    }
    QAbstractScrollArea::leaveEvent(event);
}

void CommitGraphView::keyPressEvent(QKeyEvent* event) {
    if (model_ == nullptr || model_->rowCount() == 0) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }
    const auto rows = static_cast<qsizetype>(model_->rowCount());
    const qsizetype page = std::max<qsizetype>(1, viewport()->height() / rowHeight() - 1);
    qsizetype target = selectedRow_;

    switch (event->key()) {
    case Qt::Key_Down: target = std::min(rows - 1, std::max<qsizetype>(0, selectedRow_) + 1); break;
    case Qt::Key_Up:   target = std::max<qsizetype>(0, selectedRow_ - 1); break;
    case Qt::Key_PageDown: target = std::min(rows - 1, std::max<qsizetype>(0, selectedRow_) + page); break;
    case Qt::Key_PageUp:   target = std::max<qsizetype>(0, selectedRow_ - page); break;
    case Qt::Key_Home: target = 0; break;
    case Qt::Key_End:  target = rows - 1; break;
    default:
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }

    select(target);
    scrollToRow(target);
}

void CommitGraphView::focusInEvent(QFocusEvent* event) {
    QAbstractScrollArea::focusInEvent(event);
    viewport()->update();
}

void CommitGraphView::focusOutEvent(QFocusEvent* event) {
    QAbstractScrollArea::focusOutEvent(event);
    viewport()->update();
}

void CommitGraphView::paintFocus(QPainter& painter) const {
    // The keyboard is here: one accent line round the view, the same marker
    // the stylesheet gives focused lists and fields.
    if (hasFocus()) {
        painter.setPen(QPen(tokens::selectionMarker, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(viewport()->rect().adjusted(0, 0, -1, -1));
    }
}

} // namespace gity::ui
