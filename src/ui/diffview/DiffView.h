// ADR-002 — the second hand-painted view.
//
// Unified diff, virtualized by line. Like the graph, it is a
// QAbstractScrollArea that paints itself, for the same reason: a diff of a
// generated file runs to tens of thousands of lines, and only the
// visible band should ever be touched.
//
// The row model is deliberately hunk-addressable rather than a flat list of
// text. M2 stages by hunk and by line, and building the structure now is
// cheaper than reshaping the view around it later.
#pragma once

#include "core/git/Staging.h"
#include "session/SessionTypes.h"
#include "ui/highlight/SyntaxHighlight.h"

#include <QAbstractScrollArea>

#include <memory>
#include <vector>

namespace gity::ui {

using FileDiffPtr = std::shared_ptr<const git::FileDiff>;

class DiffView : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit DiffView(QWidget* parent = nullptr);

    /// `staging` is one entry per hunk, or empty when there is nothing staged
    /// to report. SPEC.md §2 puts the count in the hunk header.
    void setDiff(FileDiffPtr diff, session::HunkStagingPtr staging = {});
    void clearDiff();
    void setPlaceholder(const QString& text);

    /// Enables line and hunk selection. Only the Changes tab wants it: a diff
    /// of a historical commit has nothing to stage.
    void setSelectable(bool selectable);

    /// Staging mode adds the pick gutter and the per-hunk buttons. Only the
    /// Changes screen wants them: a historical commit has nothing to stage.
    void setStagingMode(bool staging);
    /// Which side of the index the diff is of. On the staged side the hunk
    /// action unstages, and there is no discard: reverting the worktree to
    /// undo a *staged* change would leave the index still holding it.
    void setShowingStaged(bool staged);

    [[nodiscard]] const git::FileDiff* diff() const noexcept { return diff_.get(); }

    [[nodiscard]] const git::LineSelection& selection() const noexcept { return selection_; }

    void clearSelection();

signals:
    void selectionChanged(int lineCount);

    /// A click in the pick gutter. Immediate, not a selection — SPEC.md makes
    /// the dot the click target for staging that one line.
    void lineToggled(std::uint32_t lineIndex);
    void hunkStageRequested(std::size_t hunkIndex);
    void hunkDiscardRequested(std::size_t hunkIndex);
    /// Enter with lines selected: stage (or unstage) the selection.
    void stageSelectionRequested();

protected:
    void changeEvent(QEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void paintFocus(QPainter& painter) const;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    /// One painted row: either a hunk header or one diff line. Flattening the
    /// two into a single indexable sequence is what makes virtualization a
    /// simple range calculation.
    struct Row {
        bool isHunkHeader = false;
        std::uint32_t index = 0; ///< Into hunks or lines, per isHunkHeader.
    };

    void rebuildRows();
    void updateScrollBars();
    void paintRow(QPainter& painter, const Row& row, int y, int gutterWidth) const;

    [[nodiscard]] int rowHeight() const;
    [[nodiscard]] int gutterWidth() const;
    [[nodiscard]] QFont monoFont() const;
    /// Built once, not per call: rowHeight and paintRow ask for it many times
    /// a paint, and each build is a font database lookup.
    mutable QFont monoFont_;
    mutable bool monoFontValid_ = false;

    FileDiffPtr diff_;
    /// Per-hunk staged counts. Distinct from `staging_`, which is the *mode*
    /// this view is in; these are the numbers SPEC.md puts in the header.
    session::HunkStagingPtr hunkStaging_;
    std::vector<Row> rows_;
    [[nodiscard]] qsizetype rowAt(int viewportY) const;
    void selectRowRange(qsizetype first, qsizetype last, bool additive);

    QString placeholder_;
    git::LineSelection selection_;
    bool selectable_ = false;
    qsizetype anchorRow_ = -1;
    /// The keyboard's row, drawn as an outline when the view has focus.
    qsizetype cursorRow_ = -1;
    /// The hunk a row belongs to, or the hunk a header heads.
    [[nodiscard]] std::size_t hunkAt(qsizetype row) const;
    void moveCursor(qsizetype row, bool extend);
    bool staging_ = false;
    bool showingStaged_ = false;

    /// Painted button rectangles for the hunk header currently under the
    /// mouse press, rebuilt per paint so hit-testing uses what was drawn.
    struct HunkButtons {
        QRect stage;
        QRect discard;
    };

    mutable std::vector<HunkButtons> hunkButtons_;
    int longestLine_ = 0; ///< In characters, for the horizontal range.

    /// Colours each line from the side it belongs to: a deletion from the
    /// old file, everything else from the new. Done on the first paint of a
    /// diff, and again when the theme's colours change.
    void highlight();
    FileSpansPtr oldSpans_;
    FileSpansPtr newSpans_;
    /// Per diff line, its spans; null where there are none.
    std::vector<const LineSpans*> lineSpans_;
    QString spansKey_;
    bool spansReady_ = false;
};

} // namespace gity::ui
