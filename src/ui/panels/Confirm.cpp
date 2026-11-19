#include "Confirm.h"

#include <QAction>

#include <QAbstractButton>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QStyle>
#include <QTextDocument>

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
    // After the informative text, which the format applies to as well.
    box.setTextFormat(Qt::PlainText);
    box.exec();
    return box.clickedButton() == static_cast<QAbstractButton*>(proceed);
}

void describeCommand(QAction* action, const QString& what, const QString& command) {
    if (action == nullptr) {
        return;
    }
    action->setToolTip(plainToolTip(what.isEmpty()
                                        ? QStringLiteral("git %1").arg(command)
                                        : QStringLiteral("%1\n\ngit %2").arg(what, command)));
}

QMessageBox::StandardButton showPlainMessage(QWidget* parent, QMessageBox::Icon icon,
                                             const QString& title, const QString& text,
                                             QMessageBox::StandardButtons buttons,
                                             QMessageBox::StandardButton defaultButton,
                                             const QString& detail) {
    QMessageBox box(icon, title, text, buttons, parent);
    box.setTextFormat(Qt::PlainText);
    if (defaultButton != QMessageBox::NoButton) {
        box.setDefaultButton(defaultButton);
    }
    if (!detail.isEmpty()) {
        box.setDetailedText(detail);
    }
    box.exec();
    return box.standardButton(box.clickedButton());
}

void setPlainText(QLabel* label) {
    if (label != nullptr) {
        label->setTextFormat(Qt::PlainText);
    }
}

QString plainToolTip(const QString& text) {
    return Qt::mightBeRichText(text) ? Qt::convertFromPlainText(text, Qt::WhiteSpaceNormal) : text;
}

void setPlainLabels(QInputDialog* dialog) {
    if (dialog != nullptr) {
        for (QLabel* label : dialog->findChildren<QLabel*>()) {
            label->setTextFormat(Qt::PlainText);
        }
    }
}

void setPlainLabels(QProgressDialog* dialog) {
    if (dialog != nullptr) {
        for (QLabel* label : dialog->findChildren<QLabel*>()) {
            label->setTextFormat(Qt::PlainText);
        }
    }
}

QString chooseItem(QWidget* parent, const QString& title, const QString& label,
                   const QStringList& items, bool* accepted) {
    QInputDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setLabelText(label);
    dialog.setComboBoxItems(items);
    dialog.setComboBoxEditable(false);
    dialog.setTextValue(items.value(0));
    setPlainLabels(&dialog);
    const bool ok = dialog.exec() == QDialog::Accepted;
    if (accepted != nullptr) {
        *accepted = ok;
    }
    return ok ? dialog.textValue() : QString();
}

QString enterText(QWidget* parent, const QString& title, const QString& label, const QString& text,
                  bool* accepted) {
    QInputDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setLabelText(label);
    dialog.setTextValue(text);
    setPlainLabels(&dialog);
    const bool ok = dialog.exec() == QDialog::Accepted;
    if (accepted != nullptr) {
        *accepted = ok;
    }
    return ok ? dialog.textValue() : QString();
}

} // namespace gity::ui
