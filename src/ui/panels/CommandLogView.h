// The command log drawer: what Gity asked git to do, and how it went.
#pragma once

#include "session/GitProcess.h"

#include <QWidget>

class QCheckBox;
class QPlainTextEdit;

namespace gity::ui {

class CommandLogView : public QWidget {
    Q_OBJECT

public:
    explicit CommandLogView(QWidget* parent = nullptr);

    /// One command as the log shows it: time, repository, the command line
    /// as it could be pasted into a terminal, and how it ended.
    [[nodiscard]] static QString format(const session::GitProcess::LogRecord& record);

private:
    void rebuild();
    void add(const session::GitProcess::LogRecord& record);

    QPlainTextEdit* text_ = nullptr;
    QCheckBox* internal_ = nullptr;
};

} // namespace gity::ui
