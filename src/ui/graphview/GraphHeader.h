// SPEC.md § history — the 25px column header.
//
// A painted widget rather than a QHeaderView: the graph is a self-painting
// scroll area, not a QTreeView, so there is no header view to borrow. Sharing
// GraphColumns with the row painter is what keeps the labels over their
// columns.
#pragma once

#include <QWidget>

namespace gity::ui {

class GraphHeader : public QWidget {
    Q_OBJECT

public:
    explicit GraphHeader(QWidget* parent = nullptr);

    /// Kept in step with the view so the columns line up.
    void setMaxLanes(int maxLanes);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int maxLanes_ = 1;
};

} // namespace gity::ui
