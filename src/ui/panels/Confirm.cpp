#include "Confirm.h"

#include <QAction>

#include <QAbstractButton>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>

namespace gity::ui {

void applyRole(QWidget* widget, const QString& role) {
    if (widget == nullptr) {
        return;
    }
    widget->setProperty("gityRole", role);
    // Qt evaluates property selectors when a widget is polished, so setting
    // the property on an already-created widget changes nothing until the
    // style is asked again. Without this the roles were set, correct, and
    // completely invisible.
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

bool confirmDestructive(QWidget* parent, const QString& title, const QString& text,
                        const QString& consequence, const QString& action) {
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::Cancel, parent);
    if (!consequence.isEmpty()) {
        box.setInformativeText(consequence);
    }

    QPushButton* proceed = box.addButton(action, QMessageBox::DestructiveRole);
    // Styled from gity.qss by property, not here: the stylesheet owns every
    // colour in the chrome, and a second place to set one is how two of them
    // drift apart.
    applyRole(proceed, QStringLiteral("destructive"));

    // Cancel is the default, so return, escape and a stray keypress all do the
    // safe thing. A destructive default is how work gets lost by reflex.
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    return box.clickedButton() == static_cast<QAbstractButton*>(proceed);
}

void describeCommand(QAction* action, const QString& what, const QString& command) {
    if (action == nullptr) {
        return;
    }
    action->setToolTip(what.isEmpty() ? QStringLiteral("git %1").arg(command)
                                      : QStringLiteral("%1\n\ngit %2").arg(what, command));
}

} // namespace gity::ui
