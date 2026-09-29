#include "RefsSidebarDelegate.h"

#include "ui/theme/Tokens.h"

#include <QFontDatabase>
#include <QPainter>

namespace gity::ui {
namespace {

constexpr int kRowHeight = 23;
constexpr int kIndent = 12;
constexpr int kGlyphBox = 13;
constexpr int kGap = 8;

QColor colourFor(SidebarTone tone, bool selected) {
    if (selected) {
        return tokens::textSelectedPrimary;
    }
    switch (tone) {
    case SidebarTone::Muted:
        return tokens::textDim;
    case SidebarTone::Warn:
        return tokens::semanticWarn;
    case SidebarTone::Add:
        return tokens::semanticAdd;
    case SidebarTone::Default:
        break;
    }
    return tokens::textDefault;
}

QFont monoFont(const QFont& base, int delta) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(std::max(7, base.pointSize() + delta));
    return font;
}

} // namespace

RefsSidebarDelegate::RefsSidebarDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QSize RefsSidebarDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    return {0, kRowHeight};
}

void RefsSidebarDelegate::paint(QPainter* painter, const QStyleOptionViewItem& styleOption,
                                const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Without this, option.font is the view's font rather than the item's, and
    // the per-item fonts the section headers set are silently ignored.
    QStyleOptionViewItem option = styleOption;
    initStyleOption(&option, index);

    const QRect row = option.rect;
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const bool hovered = (option.state & QStyle::State_MouseOver) != 0;

    const bool current = index.data(CurrentRole).toBool();
    if (selected) {
        painter->fillRect(row, tokens::selectionBg);
        // The 2px marker the default selection rect cannot produce. Painted
        // at the row's left edge, not the indent, so nested rows line up.
        painter->fillRect(QRect(row.x(), row.y(), 2, row.height()), tokens::selectionMarker);
    } else if (hovered) {
        painter->fillRect(row, tokens::selectionHover);
    } else if (current) {
        // The branch you are on, findable at a glance: a weight change alone
        // was too quiet to spot in a long list.
        QColor tint = tokens::accentPrimary;
        tint.setAlpha(56);
        painter->fillRect(row, tint);
    }

    const auto tone = static_cast<SidebarTone>(index.data(ToneRole).toInt());
    const QColor foreground =
        current && !selected && tone == SidebarTone::Default ? tokens::textStrong
                                                             : colourFor(tone, selected);

    // QTreeWidget has already indented option.rect by depth, so the glyph box
    // starts at a fixed offset from wherever this row begins.
    int x = row.x() + kIndent;

    const QString glyph = index.data(GlyphRole).toString();
    if (!glyph.isEmpty()) {
        painter->setFont(monoFont(option.font, -1));
        painter->setPen(current && !selected ? tokens::accentPrimary : foreground);
        painter->drawText(QRect(x, row.y(), kGlyphBox, row.height()), Qt::AlignCenter,
                          current ? QStringLiteral("\u2713") : glyph);
    }
    x += kGlyphBox + kGap;

    // The badge is laid out first so the label knows what room is left.
    const QString badge = index.data(BadgeRole).toString();
    QRect badgeRect;
    if (!badge.isEmpty()) {
        const QFont badgeFont = monoFont(option.font, -2);
        const QFontMetrics metrics(badgeFont);
        const int width = metrics.horizontalAdvance(badge) + 10;
        badgeRect = QRect(row.right() - 10 - width, row.y() + (row.height() - 15) / 2, width, 15);
    }

    // The tag chip sits just left of the badge, outlined in the accent so it
    // reads as a property of the branch rather than a count.
    const QString tag = index.data(TagRole).toString();
    QRect tagRect;
    if (!tag.isEmpty()) {
        const QFontMetrics metrics(monoFont(option.font, -2));
        const int width = metrics.horizontalAdvance(tag) + 10;
        const int right = badgeRect.isNull() ? row.right() - 10 : badgeRect.left() - 4;
        tagRect = QRect(right - width, row.y() + (row.height() - 15) / 2, width, 15);
    }

    QFont labelFont = option.font;
    if (current) {
        labelFont.setWeight(QFont::DemiBold);
    }
    painter->setFont(labelFont);
    painter->setPen(foreground);
    const int labelWidth = (!tagRect.isNull()     ? tagRect.left() - kGap
                            : !badgeRect.isNull() ? badgeRect.left() - kGap
                                                  : row.right() - 10) -
                           x;
    const QFontMetrics labelMetrics(labelFont);
    painter->drawText(QRect(x, row.y(), std::max(0, labelWidth), row.height()),
                      Qt::AlignVCenter | Qt::AlignLeft,
                      labelMetrics.elidedText(index.data(Qt::DisplayRole).toString(),
                                              Qt::ElideMiddle, std::max(0, labelWidth)));

    if (!tag.isEmpty()) {
        painter->setPen(QPen(selected ? tokens::textSelectedPrimary : tokens::accentPrimary, 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(QRectF(tagRect).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        painter->setFont(monoFont(option.font, -2));
        painter->drawText(tagRect, Qt::AlignCenter, tag);
    }

    if (!badge.isEmpty()) {
        const auto badgeTone = static_cast<SidebarTone>(index.data(BadgeToneRole).toInt());
        // A chip, not coloured text: SPEC.md puts every badge on the same
        // neutral fill so the count reads as a count rather than as a state,
        // and the tone tints only the text on top of it.
        painter->setPen(Qt::NoPen);
        painter->setBrush(tokens::fillNeutralChip);
        painter->drawRoundedRect(badgeRect, 3, 3);
        painter->setFont(monoFont(option.font, -2));
        painter->setPen(badgeTone == SidebarTone::Default ? tokens::textDim
                                                          : colourFor(badgeTone, false));
        painter->drawText(badgeRect, Qt::AlignCenter, badge);
    }

    // The row the keyboard is on, when the list has focus: an outline, so it
    // reads as distinct from selection (2026-09 UI review).
    if ((option.state & QStyle::State_HasFocus) != 0) {
        painter->setPen(QPen(tokens::selectionMarker, 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(QRectF(option.rect).adjusted(0.5, 0.5, -0.5, -0.5));
    }

    painter->restore();
}

} // namespace gity::ui
