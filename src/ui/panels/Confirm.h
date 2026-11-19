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

#include <QMessageBox>
#include <QString>

class QAction;
class QInputDialog;
class QLabel;
class QProgressDialog;
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
[[nodiscard]] bool confirmDestructive(QWidget* parent, const QString& title, const QString& text,
                                      const QString& consequence, const QString& action);

// Text shown as written. Qt guesses whether a label's or a message box's text
// is markup, and branch, tag and file names — and what a server says back —
// may hold "<" and ">": a branch named "<b>x" would be shown in bold, and
// worse can be written. Anything carrying names or git's own words goes
// through one of these rather than the default guess.

/// A message box with its text and informative text as plain text: the
/// static QMessageBox functions, which guess. Returns the button pressed.
QMessageBox::StandardButton
showPlainMessage(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text,
                 QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                 QMessageBox::StandardButton defaultButton = QMessageBox::NoButton,
                 const QString& detail = {});

/// A label that shows its text as written.
void setPlainText(QLabel* label);

/// A tooltip that shows `text` as written. Tooltips have no text format of
/// their own, so text that would be taken for markup is escaped into it;
/// anything else is returned unchanged and looks as it always has.
[[nodiscard]] QString plainToolTip(const QString& text);

/// The prompt of an input or progress dialog, as written. Those dialogs make
/// their label themselves, so this finds it.
void setPlainLabels(QInputDialog* dialog);
void setPlainLabels(QProgressDialog* dialog);

/// QInputDialog::getItem with its prompt as written.
[[nodiscard]] QString chooseItem(QWidget* parent, const QString& title, const QString& label,
                                 const QStringList& items, bool* accepted);
/// QInputDialog::getText with its prompt as written.
[[nodiscard]] QString enterText(QWidget* parent, const QString& title, const QString& label,
                                const QString& text, bool* accepted);

} // namespace gity::ui
