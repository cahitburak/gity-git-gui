// SPEC.md §2 — the two-line working-copy row.
//
// A delegate rather than item text: the row carries a checkbox, a status
// letter, an elided name, a state chip and a second line of context, and
// QTreeWidgetItem's columns cannot lay that out.
//
// One row per changed file, carrying its own checkbox and state.
#pragma once

#include <QStyledItemDelegate>

namespace gity::ui {

/// Item data roles the delegate paints from.
enum WorkingCopyRole {
    PathRole = Qt::UserRole + 1,
    NameRole,       ///< File name alone — the part worth reading.
    DirectoryRole,  ///< Everything before it, for the sub-line.
    StatusRole,     ///< One letter: A, M, D, R, ?
    ChipRole,       ///< "partial", "conflict", "locked", or empty.
    ChipKindRole,   ///< ChipKind.
    SubtitleRole,   ///< The sub-line's trailing explanation.
    WarningRole,    ///< bool: sub-line uses the warn colour.
};

enum class ChipKind { Add, Neutral, Remove, Warn };

class WorkingCopyDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit WorkingCopyDelegate(bool staged, QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

    /// The checkbox rectangle, so the view can tell a toggle from a selection.
    [[nodiscard]] static QRect checkboxRect(const QRect& row);

private:
    bool staged_ = false;
};

} // namespace gity::ui
