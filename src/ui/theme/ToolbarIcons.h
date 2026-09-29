// The action icons, drawn rather than shipped.
//
// Simple glyphs did not justify a resource system, an icon font or a
// dependency on someone else's set — and a painted path scales to whatever
// device pixel ratio the screen has without shipping three sizes of each.
//
// They are deliberately plain: a Git client's toolbar is read many times a
// day and an icon that takes a moment to decode costs more than it saves. Each
// one is a shape you can name in a word — an arrow down for fetch, an arrow up
// for push.
#pragma once

#include <QIcon>

namespace gity::ui::icons {

enum class Glyph {
    Fetch,   ///< Arrow down into a tray.
    Pull,    ///< Arrow down, heavier.
    Push,    ///< Arrow up.
    Branch,  ///< Two lines diverging from one.
    Stash,   ///< Stacked layers.
    Merge,   ///< Two lines converging into one.
    Rebase,  ///< A line lifted onto another.

    // Context-menu verbs. Same box and weight as the toolbar's, because they
    // sit next to text at the same size and a heavier line would read as a
    // different family.
    Checkout,     ///< An arrow entering a bracket.
    NewBranch,    ///< The branch fork, with a plus.
    CherryPick,   ///< A commit lifted by an arc onto a line.
    Revert,       ///< An arc turning back on itself.
    Tag,          ///< A label with its eyelet.
    Reset,        ///< An arrow back to a bar.
    Copy,         ///< Two overlapping sheets.
    Delete,       ///< A cross: removes a reference, destroys no work.
    Discard,      ///< A bin: this one does destroy work.
    Lock,         ///< A closed padlock.
    Unlock,       ///< The same padlock, shackle open.
    StashPop,     ///< Layers with an arrow leaving them.
    StashApply,   ///< Layers with an arrow copied off them.
    Pin,          ///< A push-pin: head, collar and point.
    Settings,     ///< A gear: eight teeth around a ring.
};

/// The glyph in the theme's text colour, dimmed when disabled so a greyed
/// button does not simply vanish against the toolbar.
///
/// Resolved each time it is painted, not when it is made: an icon that keeps
/// the colour it was created with does not follow a theme switch, which is how
/// the toolbar ended up with dark-theme glyphs on the light theme's toolbar.
[[nodiscard]] QIcon themed(Glyph glyph);

} // namespace gity::ui::icons
