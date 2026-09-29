// The dialog gity-askpass puts up. Separate from that binary's main() so it
// can be rendered and inspected like every other surface in this client — a
// credential prompt is the last dialog that should be exempt from review.
#pragma once

#include "core/git/AskpassPrompt.h"

#include <QDialog>

class QLineEdit;

namespace gity::ui {

class AskpassDialog : public QDialog {
    Q_OBJECT

public:
    /// `prompt` is git's or ssh's text, passed through verbatim.
    explicit AskpassDialog(const QString& prompt, QWidget* parent = nullptr);

    [[nodiscard]] QString answer() const;

private:
    QLineEdit* field_ = nullptr;
    git::AskpassRequest request_;
};

} // namespace gity::ui
