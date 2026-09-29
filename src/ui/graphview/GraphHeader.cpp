#include "GraphHeader.h"

#include "GraphColumns.h"
#include "ui/theme/Theme.h"
#include "ui/theme/Tokens.h"

#include <QCoreApplication>
#include <QPainter>

namespace gity::ui {
namespace {

/// SPEC.md type.sectionLabel: 10px, 600, 0.09em tracking, uppercase.
QFont sectionLabelFont(const QFont& base) {
    QFont font = base;
    font.setPointSize(std::max(7, base.pointSize() - 2));
    font.setWeight(QFont::DemiBold);
    font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::PercentageSpacing, 109.0);
    return font;
}

} // namespace

GraphHeader::GraphHeader(QWidget* parent) : QWidget(parent) {
    setFixedHeight(tokens::chromeColumnHeader);
}

void GraphHeader::setMaxLanes(int maxLanes) {
    if (maxLanes_ == maxLanes) {
        return;
    }
    maxLanes_ = maxLanes;
    update();
}

QSize GraphHeader::sizeHint() const {
    return {0, tokens::chromeColumnHeader};
}

void GraphHeader::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), tokens::surfaceHeader);
    painter.setFont(sectionLabelFont(font()));

    const GraphColumns columns = GraphColumns::forWidth(width(), maxLanes_);
    const int pad = 8;

    struct Column {
        int x;
        int width;
        const char* label;
        bool rightAligned;
    };
    const Column layout[] = {
        {columns.graphX(), columns.graphWidth, "Graph · Description", false},
        {columns.messageX(), columns.messageWidth, "Message", false},
        {columns.authorX(), columns.authorWidth, "Author", false},
        {columns.dateX(), columns.dateWidth, "Date", true},
        {columns.commitX(), columns.commitWidth, "Commit", false},
    };

    for (std::size_t i = 0; i < std::size(layout); ++i) {
        const Column& column = layout[i];

        // Every column after the first is preceded by a rule, which is what
        // makes the fixed widths legible as columns rather than as spacing.
        if (i > 0) {
            painter.setPen(tokens::borderSoft);
            painter.drawLine(column.x, 0, column.x, height());
        }

        painter.setPen(tokens::textLabelDim);
        const Qt::Alignment alignment =
            Qt::AlignVCenter | (column.rightAligned ? Qt::AlignRight : Qt::AlignLeft);
        // drawText takes int flags; QFlags -> int trips -Wsign-conversion.
        painter.drawText(QRect(column.x + pad, 0, column.width - pad * 2, height()),
                         static_cast<int>(alignment.toInt()),
                         QCoreApplication::translate("GraphHeader", column.label));
    }

    painter.setPen(tokens::borderHard);
    painter.drawLine(0, height() - 1, width(), height() - 1);
}

} // namespace gity::ui
