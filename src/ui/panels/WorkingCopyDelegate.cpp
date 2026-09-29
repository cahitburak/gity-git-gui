#include "WorkingCopyDelegate.h"

#include "ui/theme/Tokens.h"

#include <QFontDatabase>
#include <QPainter>

namespace gity::ui {
namespace {

constexpr int kRowHeight = 46;
constexpr int kPaddingX = 12;
constexpr int kCheckboxSize = 13;
constexpr int kGap = 9;
constexpr int kStatusBox = 12;
constexpr int kSubIndent = 47;

struct ChipPalette {
    QColor text;
    QColor fill;
    QColor border;
};

ChipPalette paletteFor(ChipKind kind) {
    switch (kind) {
    case ChipKind::Add:
        return {tokens::semanticAdd, tokens::fillAddChip, tokens::fillAddChipBorder};
    case ChipKind::Remove:
        return {tokens::semanticRemove, tokens::fillRemoveChip, tokens::fillRemoveChipBorder};
    case ChipKind::Warn:
        return {tokens::semanticWarn, tokens::fillWarnChip, tokens::fillWarnChipBorder};
    case ChipKind::Neutral:
        break;
    }
    return {tokens::textDim, tokens::fillNeutralChip, tokens::fillNeutralChipBorder};
}

QColor statusColour(QChar letter) {
    switch (letter.toLatin1()) {
    case 'A':
    case '?':
        return tokens::semanticAdd;
    case 'D':
        return tokens::semanticRemove;
    case 'R':
        return tokens::semanticMeta;
    default:
        break;
    }
    return tokens::semanticInfo;
}

QFont monoFont(const QFont& base, int delta) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(std::max(7, base.pointSize() + delta));
    return font;
}

} // namespace

WorkingCopyDelegate::WorkingCopyDelegate(bool staged, QObject* parent)
    : QStyledItemDelegate(parent), staged_(staged) {}

QRect WorkingCopyDelegate::checkboxRect(const QRect& row) {
    return QRect(row.x() + kPaddingX, row.y() + 8, kCheckboxSize, kCheckboxSize);
}

QSize WorkingCopyDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    return {0, kRowHeight};
}

void WorkingCopyDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRect row = option.rect;
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const bool hovered = (option.state & QStyle::State_MouseOver) != 0;

    if (selected) {
        painter->fillRect(row, tokens::selectionBg);
        // The 2px marker the default selection rect cannot produce.
        painter->fillRect(QRect(row.x(), row.y(), 2, row.height()), tokens::selectionMarker);
    } else if (hovered) {
        painter->fillRect(row, tokens::selectionHover);
    }

    // --- checkbox -------------------------------------------------------
    // Filled when staged, empty when not: the checkbox *is* the staged state,
    // so there is no separate control to fall out of step with it.
    const QRect box = checkboxRect(row);
    if (staged_) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(tokens::accentPrimary);
        painter->drawRoundedRect(box, 3, 3);

        painter->setPen(QPen(tokens::surfaceWindow, 1.8));
        painter->drawLine(box.left() + 3, box.center().y(), box.center().x() - 1,
                          box.bottom() - 3);
        painter->drawLine(box.center().x() - 1, box.bottom() - 3, box.right() - 2,
                          box.top() + 3);
    } else {
        painter->setPen(QPen(tokens::borderCheckbox, 1));
        painter->setBrush(tokens::surfaceCheckbox);
        painter->drawRoundedRect(box, 3, 3);
    }

    int x = box.right() + kGap;

    // --- status letter --------------------------------------------------
    const QString status = index.data(StatusRole).toString();
    if (!status.isEmpty()) {
        painter->setFont(monoFont(option.font, 0));
        painter->setPen(statusColour(status.at(0)));
        painter->drawText(QRect(x, row.y() + 5, kStatusBox, 18),
                          Qt::AlignVCenter | Qt::AlignLeft, status);
    }
    x += kStatusBox + kGap;

    // --- chip, laid out first so the name knows how much room it has -----
    const QString chip = index.data(ChipRole).toString();
    int chipWidth = 0;
    QRect chipRect;
    if (!chip.isEmpty()) {
        QFont chipFont = option.font;
        chipFont.setPointSize(std::max(7, option.font.pointSize() - 2));
        const QFontMetrics fm(chipFont);
        chipWidth = fm.horizontalAdvance(chip) + 12;
        chipRect = QRect(row.right() - kPaddingX - chipWidth, row.y() + 6, chipWidth,
                         fm.height() + 3);
    }

    // --- name -----------------------------------------------------------
    const int nameWidth =
        row.right() - kPaddingX - x - (chipWidth > 0 ? chipWidth + kGap : 0);
    const QFontMetrics nameMetrics(option.font);
    painter->setFont(option.font);
    painter->setPen(selected ? tokens::textSelectedPrimary : tokens::textDefault);
    painter->drawText(QRect(x, row.y() + 5, nameWidth, 18), Qt::AlignVCenter | Qt::AlignLeft,
                      nameMetrics.elidedText(index.data(NameRole).toString(), Qt::ElideMiddle,
                                             nameWidth));

    if (!chip.isEmpty()) {
        const ChipPalette palette =
            paletteFor(static_cast<ChipKind>(index.data(ChipKindRole).toInt()));
        QFont chipFont = option.font;
        chipFont.setPointSize(std::max(7, option.font.pointSize() - 2));
        painter->setFont(chipFont);
        painter->setPen(QPen(palette.border, 1));
        painter->setBrush(palette.fill);
        painter->drawRoundedRect(chipRect, tokens::radiusChip, tokens::radiusChip);
        painter->setPen(palette.text);
        painter->drawText(chipRect, Qt::AlignCenter, chip);
    }

    // --- sub-line -------------------------------------------------------
    // Directory plus state: the name answers "what", this answers "where and
    // in what condition", which is the pair a reviewer actually needs.
    const QString directory = index.data(DirectoryRole).toString();
    const QString subtitle = index.data(SubtitleRole).toString();
    QString second = directory;
    if (!subtitle.isEmpty()) {
        second += second.isEmpty() ? subtitle : QStringLiteral(" · ") + subtitle;
    }
    if (!second.isEmpty()) {
        painter->setFont(monoFont(option.font, -2));
        painter->setPen(index.data(WarningRole).toBool() ? tokens::semanticWarnSoft
                                                         : tokens::textLabel);
        const QFontMetrics subMetrics(painter->font());
        const int subWidth = row.right() - kPaddingX - (row.x() + kSubIndent);
        painter->drawText(QRect(row.x() + kSubIndent, row.y() + 24, subWidth, 16),
                          Qt::AlignVCenter | Qt::AlignLeft,
                          subMetrics.elidedText(second, Qt::ElideMiddle, subWidth));
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
