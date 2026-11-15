// One column of the conflict editor: ours, the result, or theirs.
//
// Painted, like the diff (ADR-002): a conflicted generated file can run to
// tens of thousands of lines. Each region is tinted by whose change it holds —
// ours in the accent blue, theirs in violet — and a conflict carries buttons
// on its first row to take a side. In the result, an unresolved conflict says
// so in place, and a double-click edits a region's lines where they stand.
#pragma once

#include "ConflictDocument.h"
#include "ui/highlight/SyntaxHighlight.h"

#include <QAbstractScrollArea>
#include <QPointer>

#include <optional>
#include <vector>

class QPainter;
class QPlainTextEdit;

namespace gity::ui {

class MergePane : public QAbstractScrollArea {
    Q_OBJECT

public:
    MergePane(ConflictDocument* document, ConflictDocument::Column column,
              QWidget* parent = nullptr);

    [[nodiscard]] ConflictDocument::Column column() const noexcept { return column_; }

    /// The conflict being worked on, marked down its left edge.
    void setCurrentRegion(std::optional<std::size_t> region);
    /// Opens the lines of `region` for typing, in this pane. Only the result
    /// pane edits.
    void editRegion(std::size_t region);
    /// Ends an edit in progress, keeping what was typed.
    void finishEditing();

    [[nodiscard]] int rowHeight() const;

signals:
    /// A region was clicked: it becomes the current one.
    void regionActivated(std::size_t region);
    /// "Edit Result…" chosen where there is no result column to edit in.
    void editRequested(std::size_t region);

protected:
    void changeEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class Action { Pick, Edit };

    struct Chip {
        QRect rect;
        std::size_t region = 0;
        Action action = Action::Pick;
        git::Pick pick = git::Pick::Unresolved;
        QString label;
        bool taken = false;
    };

    void updateScrollBars();
    void placeEditor();
    void stopEditing(bool keep);
    [[nodiscard]] QFont monoFont() const;
    [[nodiscard]] int gutterWidth() const;
    [[nodiscard]] int charWidth() const;
    [[nodiscard]] std::optional<std::size_t> regionAt(int viewportY) const;
    [[nodiscard]] const Chip* chipAt(const QPoint& point) const;
    /// The chips a region's first row carries in this column.
    [[nodiscard]] std::vector<Chip> chipsFor(std::size_t region, int y, int left) const;
    /// A conflict's first row: which one it is, and its buttons.
    void paintHeader(QPainter& painter, std::size_t region, int y);
    [[nodiscard]] QColor bandFor(std::size_t region) const;
    [[nodiscard]] bool dimmed(std::size_t region) const;

    /// Colours for this column's file, made when first painted and again when
    /// its text or the theme changes.
    void highlight();
    FileSpansPtr spans_;
    QString spansKey_;
    const void* spansFile_ = nullptr;
    bool spansReady_ = false;

    ConflictDocument* document_;
    ConflictDocument::Column column_;
    std::optional<std::size_t> current_;
    std::vector<Chip> chips_;
    int longestLine_ = 0;

    QPointer<QPlainTextEdit> editor_;
    std::size_t editing_ = 0;
    /// The text the editor opened with, to tell an edit from a look.
    QString editStart_;

    mutable QFont monoFont_;
    mutable bool monoFontValid_ = false;
};

} // namespace gity::ui
