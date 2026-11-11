// The two dialogs that move the working copy to another commit: Checkout,
// when there are uncommitted changes, and New Branch. Both ask up front what
// happens to the changes and whether submodules follow, rather than leaving
// it to be discovered afterwards.
#pragma once

#include "session/SwitchContext.h"

#include <QCoreApplication>
#include <QDialog>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QVBoxLayout;

namespace gity::ui {

/// What to do with uncommitted changes when switching. Shared by both.
class ChangesChoice {
    Q_DECLARE_TR_FUNCTIONS(ChangesChoice)

public:
    ChangesChoice(QDialog* dialog, QVBoxLayout* layout, const QString& destination);

    [[nodiscard]] gity::session::LocalChanges value() const;
    void setVisible(bool visible) const;
    /// Explains the chosen option, and makes `go` say what it does: red, and
    /// naming the discard, when that is what it will do.
    void describe(QPushButton* go, const QString& text, const QString& discardText) const;
    /// Calls `changed` whenever the choice changes.
    void onChanged(QDialog* dialog, const std::function<void()>& changed) const;

private:
    QRadioButton* keep_ = nullptr;
    QRadioButton* stash_ = nullptr;
    QRadioButton* discard_ = nullptr;
    QLabel* explain_ = nullptr;
};

/// Switching to `ref` with `changedFiles` uncommitted changes.
class CheckoutDialog : public QDialog {
    Q_OBJECT

public:
    /// `headName` is the branch checked out now, for the wording.
    CheckoutDialog(const QString& ref, const gity::session::SwitchContext& context,
                   int changedFiles, const QString& headName, QWidget* parent = nullptr);
    ~CheckoutDialog() override;

    [[nodiscard]] gity::session::LocalChanges changes() const;
    [[nodiscard]] bool updateSubmodules() const;

private:
    std::unique_ptr<ChangesChoice> choice_;
    QCheckBox* update_ = nullptr;
    bool hasSubmodules_ = false;
};

/// A new branch at `base` (empty: the current commit), optionally switched to.
class NewBranchDialog : public QDialog {
    Q_OBJECT

public:
    /// `existing` are the local branch names, refused as the new one's.
    NewBranchDialog(const QString& base, const gity::session::SwitchContext& context,
                    int changedFiles, const QStringList& existing, QWidget* parent = nullptr);
    ~NewBranchDialog() override;

    [[nodiscard]] QString name() const;
    [[nodiscard]] bool switchesTo() const;
    /// Only asked when switching moves the working copy; Keep otherwise.
    [[nodiscard]] gity::session::LocalChanges changes() const;
    [[nodiscard]] bool updateSubmodules() const;

private:
    void refresh();

    gity::session::SwitchContext context_;
    QStringList existing_;
    bool askChanges_ = false;
    QLineEdit* name_ = nullptr;
    QLabel* problem_ = nullptr;
    QCheckBox* checkout_ = nullptr;
    QLabel* changesIntro_ = nullptr;
    std::unique_ptr<ChangesChoice> choice_;
    QCheckBox* update_ = nullptr;
    QPushButton* go_ = nullptr;
};

} // namespace gity::ui
