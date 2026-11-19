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
    /// An edit is open and its text is not what it opened with: finishing it
    /// would change the result.
    [[nodiscard]] bool editPending() const;

    [[nodiscard]] int rowHeight() const;
    /// How far down this pane scrolls, for its own height, to show its last
    /// row with a little room under it.
    [[nodiscard]] int verticalReach() const;
    /// Lets it scroll at least as far as `reach`: the panes beside it scroll
    /// with it, and one that stopped short would hold the others back.
    void setSharedReach(int reach);

signals:
    /// A region was clicked: it becomes the current one.
    void regionActivated(std::size_t region);
    /// "Edit Result…" chosen where there is no result column to edit in.
    void editRequested(std::size_t region);
    /// The text of an open edit changed.
    void editChanged();
    /// verticalReach() may have changed.
    void reachChanged();

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

    /// A conflict's first row as painted: which one it is, and its buttons.
    struct Header {
        QString label;
        QColor tone;
        std::vector<Chip> chips;
    };

    void updateScrollBars();
    void placeEditor();
    void stopEditing(bool keep);
    [[nodiscard]] QFont monoFont() const;
    [[nodiscard]] int gutterWidth() const;
    [[nodiscard]] int charWidth() const;
    [[nodiscard]] std::optional<std::size_t> regionAt(int viewportY) const;
    /// Worked out from the rows where the point is, not from what the last
    /// paint drew: a paint of part of the view draws only some of them.
    [[nodiscard]] std::optional<Chip> chipAt(const QPoint& point) const;
    /// The chips a region's first row carries in this column.
    [[nodiscard]] std::vector<Chip> chipsFor(std::size_t region, int y, int left) const;
    /// The header of `region` on the row at `y`, laid out to the pane's width.
    [[nodiscard]] Header header(std::size_t region, int y) const;
    void paintHeader(QPainter& painter, std::size_t region, int y);
    [[nodiscard]] QColor bandFor(std::size_t region) const;
    [[nodiscard]] bool dimmed(std::size_t region) const;

    /// Colours for this column's file, made when first painted and again when
    /// its text or the theme changes.
    void highlight();
    FileSpansPtr spans_;
    QString spansKey_;
    /// Which file they are of. Weak, not a raw address: a file freed and
    /// another made where it was would pass for it.
    session::ConflictPtr::weak_type spansFile_;
    bool spansReady_ = false;

    ConflictDocument* document_;
    ConflictDocument::Column column_;
    std::optional<std::size_t> current_;
    int longestLine_ = 0;
    int sharedReach_ = 0;

    QPointer<QPlainTextEdit> editor_;
    std::size_t editing_ = 0;
    /// The text the editor opened with, to tell an edit from a look.
    QString editStart_;

    mutable QFont monoFont_;
    mutable bool monoFontValid_ = false;
};

} // namespace gity::ui
