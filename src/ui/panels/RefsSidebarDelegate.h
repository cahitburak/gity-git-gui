// SPEC.md §0 — the sidebar row.
//
// A delegate rather than columns: the row is a fixed glyph box, an elided
// label and a right-aligned badge drawn as a chip, and QTreeWidget's column
// layout cannot produce the chip or the 2px selection marker. Nesting is by
// item depth, so a remote's branches indent under it without a second widget.
#pragma once

#include <QStyledItemDelegate>

namespace gity::ui {

enum SidebarRole {
    GlyphRole = Qt::UserRole + 10,
    BadgeRole,      ///< Right-aligned chip text. Empty hides it.
    BadgeToneRole,  ///< SidebarTone for the badge.
    CurrentRole,    ///< bool: the checked-out branch — tinted, ticked, heavier.
    ToneRole,       ///< SidebarTone for the glyph and label.
    TagRole,        ///< An outlined accent chip before the badge, e.g. "default".
};

enum class SidebarTone { Default, Muted, Warn, Add };

class RefsSidebarDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit RefsSidebarDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};

} // namespace gity::ui
