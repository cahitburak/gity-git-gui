// Confirming an action that cannot be undone.
//
// One place, for two reasons. A destructive confirmation has a shape — say
// what goes, say whether it can come back, name the action on its own button
// rather than "Yes" — and repeating that shape by hand at a dozen call sites
// is how one of them ends up saying "Yes/No" about deleting someone's work.
//
// And the colour belongs with it. Red marks the button that destroys
// something, but red is never the only signal: the button also carries the
// verb, because roughly one man in twelve cannot separate red from green and
// a colour-only warning tells them nothing.
#pragma once

#include <QString>

class QAction;
class QWidget;

namespace gity::ui {

/// Marks a widget with an action role — "destructive", "primary" — and
/// re-polishes it so the stylesheet actually applies.
///
/// The re-polish is the point. Qt evaluates property selectors when a widget
/// is polished, so setting the property alone leaves the role correct and
/// invisible, which is exactly what happened the first time.
void applyRole(QWidget* widget, const QString& role);

/// A tooltip that says what an action does and then the git command behind
/// it, as it could be typed: plain language first, git one hover away. Null
/// actions are ignored, so optional menu entries need no guard.
void describeCommand(QAction* action, const QString& what, const QString& command);

/// Asks before something that cannot be undone. Returns true to proceed.
///
/// `action` is the label on the destructive button — "Discard", "Delete
/// Anyway" — never "Yes". Cancel is the default, so return does the safe
/// thing.
[[nodiscard]] bool confirmDestructive(QWidget* parent, const QString& title,
                                      const QString& text, const QString& consequence,
                                      const QString& action);

} // namespace gity::ui
