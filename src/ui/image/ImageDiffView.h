// SPEC.md §3 — binary asset diff.
//
// The three modes are one cached composite, not three renderers: difference
// and onion-skin are composition modes over the same decoded pair, recomputed
// on revision change rather than on repaint. Rebuilding a 2048² composite
// every paint is the obvious way to make this unusable.
#pragma once

#include "session/ImageDiff.h"

#include <QWidget>

class QLabel;

namespace gity::ui {

class ImageDiffView : public QWidget {
    Q_OBJECT

public:
    enum class Mode { SideBySide, Difference, OnionSkin };

    explicit ImageDiffView(QWidget* parent = nullptr);

    void setDiff(session::ImageDiffPtr diff);
    void clearDiff();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void setMode(Mode mode);
    void rebuildComposite();
    void paintPane(QPainter& painter, const QRect& area, const QString& label,
                   const QString& metadata, const QColor& labelColour,
                   const QImage& image) const;

    session::ImageDiffPtr diff_;
    Mode mode_ = Mode::SideBySide;

    /// Recomputed only when the pair or the mode changes.
    QImage composite_;

    QLabel* path_ = nullptr;
    QLabel* lfsChip_ = nullptr;
    QWidget* modeBar_ = nullptr;
};

} // namespace gity::ui
