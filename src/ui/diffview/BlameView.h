// A file with, beside each line, the commit that last changed it.
//
// Painted like the diff (ADR-002): a blame of a generated file runs to tens
// of thousands of lines, and only the visible band is ever touched. Lines are
// grouped into runs from the same commit; a run's commit is named once, at
// its top, and runs alternate in shade so where one ends is visible.
#pragma once

#include "session/SessionTypes.h"

#include <QAbstractScrollArea>
#include <QString>

#include <vector>

namespace gity::ui {

class BlameView : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit BlameView(QWidget* parent = nullptr);

    void setBlame(session::BlamePtr blame);
    void clearBlame();
    /// Shown while there is no blame.
    void setPlaceholder(const QString& text);

    [[nodiscard]] bool hasBlame() const noexcept { return blame_ != nullptr; }

signals:
    /// A line was clicked: its commit, by full id. Not sent for lines not
    /// committed yet.
    void commitActivated(const QString& id);
    /// "Blame Before This Change": the commit before `id` that had the file,
    /// and the file's name there.
    void previousRequested(const QString& previousId, const QString& previousPath);

protected:
    void changeEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool viewportEvent(QEvent* event) override;

private:
    /// A commit as the gutter shows it, worked out once per blame.
    struct Label {
        QString shortId;
        QString author;
        QString date;
        QString tooltip;
        bool uncommitted = false;
    };

    void updateScrollBars();
    [[nodiscard]] QFont monoFont() const;
    [[nodiscard]] int rowHeight() const;
    /// The who-and-when column, then the line numbers.
    [[nodiscard]] int labelWidth() const;
    [[nodiscard]] int numberWidth() const;
    [[nodiscard]] qsizetype rowAt(int viewportY) const;

    session::BlamePtr blame_;
    std::vector<Label> labels_;
    /// Each line's text with tabs expanded.
    std::vector<QString> texts_;
    /// Each line's run, counted from the top: odd runs are shaded.
    std::vector<std::uint32_t> runs_;
    int longestLine_ = 0;
    /// The commit whose lines are picked out, or -1.
    qsizetype highlighted_ = -1;
    QString placeholder_;

    mutable QFont monoFont_;
    mutable bool monoFontValid_ = false;
};

} // namespace gity::ui
