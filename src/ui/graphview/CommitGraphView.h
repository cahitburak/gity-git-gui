// ADR-002 — the commit graph is not built from widgets.
//
// It is a QAbstractScrollArea that paints itself, because at a million rows a
// general-purpose view framework stops helping and starts costing. Only the
// visible band is ever touched, and each row is drawn from its own record
// without consulting its neighbours (ADR-005).
//
// The renderer deliberately takes a QPainter and a geometry rather than
// reaching for widget state, so this stays portable if ADR-002 is ever
// revisited.
#pragma once

#include "ui/model/HistoryModel.h"

#include <QAbstractScrollArea>

namespace gity::ui {

class CommitGraphView : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit CommitGraphView(QWidget* parent = nullptr);

    void setModel(HistoryModel* model);

    /// Called when rows have been appended, so the scrollbars grow while the
    /// walk is still streaming.
    void rowsAppended();

    [[nodiscard]] qsizetype selectedRow() const noexcept { return selectedRow_; }

    /// Selects a row programmatically — used to land on the newest commit as
    /// soon as the first chunk arrives, so the detail panel is never blank.
    void selectRow(qsizetype row);
    /// What an empty view says. Empty text restores the default, which is
    /// right for no repository; a filter with no matches says so instead.
    void setPlaceholder(const QString& text);
    /// No row selected — when what is shown below is not one commit, such as
    /// a comparison of two branches.
    void clearSelection();

signals:
    void rowSelected(qsizetype row);
    /// Right-click on a row. The row is selected first, so the acting commit
    /// is always the one under the cursor and never a stale selection.
    void rowContextMenuRequested(qsizetype row, const QPoint& globalPos);

protected:
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void paintFocus(QPainter& painter) const;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    /// Watches the viewport for right-clicks.
    ///
    /// Not `contextMenuEvent`: a QAbstractScrollArea does not forward that
    /// event from its viewport, so the override is never called once the view
    /// is nested — it only appeared to work when the view was a top-level
    /// window and the event propagated to its parent. Not the viewport's
    /// CustomContextMenu policy either: the scroll area filters the event
    /// before the viewport's own handling emits anything.
    ///
    /// A filter installed here runs before the scroll area's, because filters
    /// run in reverse order of installation and its own is installed in its
    /// constructor.
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updateScrollBars();
    void paintRow(QPainter& painter, qsizetype row, int y) const;
    void paintEdges(QPainter& painter, const RowView& view, int y) const;

    [[nodiscard]] int rowHeight() const;
    [[nodiscard]] int laneX(int lane) const;
    [[nodiscard]] int graphGutterWidth() const;
    [[nodiscard]] qsizetype rowAt(int viewportY) const;
    /// Scrolls just far enough that `row` is fully visible.
    void scrollToRow(qsizetype row);
    void select(qsizetype row);

    HistoryModel* model_ = nullptr;
    qsizetype selectedRow_ = -1;
    qsizetype hoverRow_ = -1;
    QString placeholder_;
    int layoutGeneration_ = 0;
};

} // namespace gity::ui
