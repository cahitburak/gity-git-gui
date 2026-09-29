// "You are in the middle of a merge."
//
// A repository stopped mid-merge, mid-cherry-pick or mid-rebase is not in an
// error state, but it is in one where the next useful action is not the one
// the rest of the window offers. The banner says what is underway and gives
// the only two answers that exist: finish it, or abandon it.
//
// It names the operation rather than saying "conflict" because the operation
// decides which command finishes it, and offering `merge --abort` to someone
// mid-rebase would discard different work than they expect.
#pragma once

#include "core/git/OperationState.h"

#include <QWidget>

class QLabel;
class QPushButton;

namespace gity::ui {

class OperationBanner : public QWidget {
    Q_OBJECT

public:
    explicit OperationBanner(QWidget* parent = nullptr);

    /// Hides itself when the operation is None, so callers do not have to.
    void setOperation(git::Operation operation, const QString& noun);
    /// A rebase stopped to edit the commit with this summary, its changes
    /// staged; empty when not. Changes what the banner says, not what it does.
    void setEditing(const QString& subject);

signals:
    void continueRequested();
    void abortRequested();

private:
    QLabel* text_ = nullptr;
    QString editing_;
    git::Operation operation_ = git::Operation::None;
    QString noun_;
    QPushButton* continue_ = nullptr;
    QPushButton* abort_ = nullptr;
};

} // namespace gity::ui
