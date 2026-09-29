#include "ToolbarIcons.h"

#include <cmath>

#include "ui/theme/Tokens.h"

#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace gity::ui::icons {
namespace {

/// Everything is drawn in a 16x16 box and scaled by the caller's pixel ratio,
/// so the same path serves every display.
constexpr qreal kBox = 16.0;

void drawArrow(QPainter& painter, bool down) {
    QPainterPath path;
    if (down) {
        path.moveTo(8, 2.5);
        path.lineTo(8, 10.5);
        path.moveTo(4.5, 7);
        path.lineTo(8, 10.5);
        path.lineTo(11.5, 7);
    } else {
        path.moveTo(8, 13.5);
        path.lineTo(8, 5.5);
        path.moveTo(4.5, 9);
        path.lineTo(8, 5.5);
        path.lineTo(11.5, 9);
    }
    painter.drawPath(path);
}

/// One line that splits: the shape of the thing itself. Shared so a new
/// branch is visibly the same object with something added.
void drawFork(QPainter& painter) {
    painter.drawLine(QPointF(5, 3), QPointF(5, 13));
    QPainterPath fork;
    fork.moveTo(5, 8);
    fork.cubicTo(9, 8, 11, 7, 11, 4);
    painter.drawPath(fork);
    painter.setBrush(painter.pen().color());
    painter.drawEllipse(QPointF(5, 13), 1.6, 1.6);
    painter.drawEllipse(QPointF(11, 3.5), 1.6, 1.6);
    painter.setBrush(Qt::NoBrush);
}

void drawGlyph(QPainter& painter, Glyph glyph) {
    switch (glyph) {
    case Glyph::Fetch:
        drawArrow(painter, true);
        // The tray it lands in, so fetch reads as "bring down", not "download
        // something to nowhere".
        painter.drawLine(QPointF(3.5, 13.5), QPointF(12.5, 13.5));
        break;
    case Glyph::Pull:
        drawArrow(painter, true);
        painter.drawLine(QPointF(3.5, 13.5), QPointF(12.5, 13.5));
        painter.drawLine(QPointF(3.5, 11.5), QPointF(4.5, 11.5));
        painter.drawLine(QPointF(11.5, 11.5), QPointF(12.5, 11.5));
        break;
    case Glyph::Push:
        drawArrow(painter, false);
        painter.drawLine(QPointF(3.5, 2.5), QPointF(12.5, 2.5));
        break;
    case Glyph::Branch:
        drawFork(painter);
        break;
    case Glyph::Stash:
        for (int i = 0; i < 3; ++i) {
            const qreal y = 4.5 + (i * 3.2);
            painter.drawLine(QPointF(3.5, y), QPointF(12.5, y));
        }
        break;
    case Glyph::Merge: {
        painter.drawLine(QPointF(11, 3), QPointF(11, 13));
        QPainterPath join;
        join.moveTo(5, 3.5);
        join.cubicTo(5, 7, 7, 8, 11, 8);
        painter.drawPath(join);
        painter.setBrush(painter.pen().color());
        painter.drawEllipse(QPointF(5, 3.5), 1.6, 1.6);
        painter.drawEllipse(QPointF(11, 13), 1.6, 1.6);
        painter.setBrush(Qt::NoBrush);
        break;
    }
    case Glyph::Rebase:
        painter.drawLine(QPointF(3.5, 12), QPointF(12.5, 12));
        painter.drawLine(QPointF(3.5, 5), QPointF(9, 5));
        painter.drawLine(QPointF(9, 5), QPointF(12.5, 5));
        painter.drawLine(QPointF(10.5, 3), QPointF(12.5, 5));
        painter.drawLine(QPointF(10.5, 7), QPointF(12.5, 5));
        break;
    case Glyph::Checkout:
        // Into a bracket: you are moving somewhere, not confirming something.
        // A tick here would read as "done".
        painter.drawLine(QPointF(2.5, 8), QPointF(9, 8));
        painter.drawLine(QPointF(6.5, 5), QPointF(9.5, 8));
        painter.drawLine(QPointF(6.5, 11), QPointF(9.5, 8));
        painter.drawPolyline(QPolygonF{QPointF(11.5, 3.5), QPointF(13.5, 3.5),
                                       QPointF(13.5, 12.5), QPointF(11.5, 12.5)});
        break;
    case Glyph::NewBranch:
        drawFork(painter);
        painter.drawLine(QPointF(11, 10.5), QPointF(11, 14.5));
        painter.drawLine(QPointF(9, 12.5), QPointF(13, 12.5));
        break;
    case Glyph::CherryPick: {
        // A commit lifted off one line and set down on another.
        //
        // The first attempt ran the arc into a filled dot and the two merged
        // into a blob at 16px. The arc now stops short and ends in an
        // arrowhead, and the dot it came from sits on the line below.
        painter.drawLine(QPointF(2.5, 13), QPointF(13.5, 13));
        painter.setBrush(painter.pen().color());
        painter.drawEllipse(QPointF(4.5, 13), 1.6, 1.6);
        painter.setBrush(Qt::NoBrush);

        QPainterPath arc;
        arc.moveTo(5, 11);
        arc.cubicTo(6.5, 5.5, 10, 4.5, 11.8, 4.5);
        painter.drawPath(arc);
        painter.drawLine(QPointF(11.8, 4.5), QPointF(9.4, 3.2));
        painter.drawLine(QPointF(11.8, 4.5), QPointF(9.6, 6.4));
        break;
    }
    case Glyph::Revert: {
        // An arc turning back the way it came.
        QPainterPath arc;
        arc.moveTo(4, 10.5);
        arc.cubicTo(4, 4, 12, 4, 12, 10.5);
        painter.drawPath(arc);
        painter.drawLine(QPointF(4, 10.5), QPointF(1.8, 8));
        painter.drawLine(QPointF(4, 10.5), QPointF(6.4, 8.4));
        break;
    }
    case Glyph::Tag:
        painter.drawPolyline(QPolygonF{QPointF(2.8, 8.6), QPointF(8.2, 3.2),
                                       QPointF(13.2, 3.2), QPointF(13.2, 8.2),
                                       QPointF(7.8, 13.6), QPointF(2.8, 8.6)});
        painter.drawEllipse(QPointF(10.6, 5.8), 1.1, 1.1);
        break;
    case Glyph::Reset:
        // Back to a fixed point, which is what a reset is.
        painter.drawLine(QPointF(3.5, 3.5), QPointF(3.5, 12.5));
        painter.drawLine(QPointF(3.5, 8), QPointF(13, 8));
        painter.drawLine(QPointF(7, 4.6), QPointF(3.5, 8));
        painter.drawLine(QPointF(7, 11.4), QPointF(3.5, 8));
        break;
    case Glyph::Copy:
        painter.drawRect(QRectF(2.8, 2.8, 7.4, 7.4));
        painter.drawRect(QRectF(5.8, 5.8, 7.4, 7.4));
        break;
    case Glyph::Delete:
        // A cross, not a bin: deleting a branch or a tag removes a pointer and
        // destroys no work, and the two should not look alike.
        painter.drawLine(QPointF(4, 4), QPointF(12, 12));
        painter.drawLine(QPointF(12, 4), QPointF(4, 12));
        break;
    case Glyph::Discard:
        // A bin, because this one really does throw work away.
        painter.drawLine(QPointF(2.8, 4.6), QPointF(13.2, 4.6));
        painter.drawLine(QPointF(6.4, 4.6), QPointF(6.4, 2.8));
        painter.drawLine(QPointF(6.4, 2.8), QPointF(9.6, 2.8));
        painter.drawLine(QPointF(9.6, 2.8), QPointF(9.6, 4.6));
        painter.drawPolyline(QPolygonF{QPointF(4.2, 4.6), QPointF(5, 13.4),
                                       QPointF(11, 13.4), QPointF(11.8, 4.6)});
        break;
    case Glyph::Settings: {
        // A gear, the sign for settings everywhere: eight short teeth on a
        // ring, and the hub, all at the toolbar's line weight.
        // Teeth are short and heavy — thin ones read as a sun at 16px.
        const QPointF centre(8, 8);
        painter.drawEllipse(centre, 4.3, 4.3);
        painter.drawEllipse(centre, 1.7, 1.7);
        QPen tooth = painter.pen();
        tooth.setWidthF(tooth.widthF() * 2.2);
        tooth.setCapStyle(Qt::FlatCap);
        painter.setPen(tooth);
        for (int i = 0; i < 8; ++i) {
            const qreal angle = i * 3.14159265358979 / 4.0;
            const QPointF direction(std::cos(angle), std::sin(angle));
            painter.drawLine(centre + direction * 4.6, centre + direction * 6.6);
        }
        break;
    }
    case Glyph::Pin:
        // Head, the collar across it, and the point, leaning as a pin pressed
        // into a board does.
        painter.drawPolyline(QPolygonF{QPointF(6.2, 2.8), QPointF(10.4, 2.8)});
        painter.drawPolyline(QPolygonF{QPointF(7.0, 2.8), QPointF(6.6, 7.4),
                                       QPointF(9.6, 7.4), QPointF(9.4, 2.8)});
        painter.drawLine(QPointF(4.6, 7.4), QPointF(11.6, 7.4));
        painter.drawLine(QPointF(8.1, 7.4), QPointF(8.1, 13.4));
        break;
    case Glyph::Lock:
    case Glyph::Unlock: {
        painter.drawRect(QRectF(3.4, 7.8, 8, 5.8));
        QPainterPath shackle;
        if (glyph == Glyph::Lock) {
            // Closed: both legs meet the body, centred over it.
            shackle.moveTo(5.4, 7.8);
            shackle.lineTo(5.4, 5.6);
            shackle.cubicTo(5.4, 3.0, 9.4, 3.0, 9.4, 5.6);
            shackle.lineTo(9.4, 7.8);
        } else {
            // Open, and visibly so: the shackle is hinged off the *right* edge
            // and swung clear of the body entirely. Merely shortening one leg
            // was indistinguishable from the closed one at this size.
            shackle.moveTo(9.4, 7.8);
            shackle.lineTo(9.4, 5.6);
            shackle.cubicTo(9.4, 3.0, 13.4, 3.0, 13.4, 5.6);
        }
        painter.drawPath(shackle);
        break;
    }
    case Glyph::StashPop:
    case Glyph::StashApply:
        for (int i = 0; i < 3; ++i) {
            const qreal y = 8.0 + (i * 2.6);
            painter.drawLine(QPointF(2.8, y), QPointF(9.4, y));
        }
        if (glyph == Glyph::StashPop) {
            painter.drawLine(QPointF(12, 6.5), QPointF(12, 1.8));
            painter.drawLine(QPointF(10.2, 3.6), QPointF(12, 1.8));
            painter.drawLine(QPointF(13.8, 3.6), QPointF(12, 1.8));
        } else {
            painter.drawLine(QPointF(12, 2), QPointF(12, 6.6));
            painter.drawLine(QPointF(10.2, 4.8), QPointF(12, 6.6));
            painter.drawLine(QPointF(13.8, 4.8), QPointF(12, 6.6));
        }
        break;
    }
}

/// Draws at paint time from the current tokens, so a theme switch reaches
/// icons that were created before it. A QIcon built from pixmaps keeps the
/// colour it was made with — which left the toolbar's glyphs drawn for the
/// dark theme on the light one's pale toolbar.
class ThemedEngine final : public QIconEngine {
public:
    explicit ThemedEngine(Glyph glyph) : glyph_(glyph) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode,
               QIcon::State) override {
        const QColor colour = mode == QIcon::Disabled ? tokens::textLabelDim
                                                      : tokens::textDefault;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->translate(rect.topLeft());
        painter->scale(rect.width() / kBox, rect.height() / kBox);
        QPen pen(colour, 1.4);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        drawGlyph(*painter, glyph_);
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state,
                         qreal scale) override {
        QPixmap pixmap(size * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return pixmap;
    }

    [[nodiscard]] QIconEngine* clone() const override { return new ThemedEngine(glyph_); }

private:
    Glyph glyph_;
};

} // namespace

QIcon themed(Glyph glyph) {
    return QIcon(new ThemedEngine(glyph));
}

} // namespace gity::ui::icons
