// Every git command Gity runs, in the order it ran.
//
// The point is trust and teaching at once: a beginner sees the real command
// behind each button and learns git rather than Gity, and an expert can check
// exactly what was done to their repository. Commands come from any thread;
// they are collected here, on the UI thread, and kept to a bounded history.
#pragma once

#include "session/GitProcess.h"

#include <QObject>

#include <deque>

namespace gity::session {

class CommandLog : public QObject {
    Q_OBJECT

public:
    /// The application's log. Created on first use, owned by the application.
    static CommandLog* instance();

    /// Routes GitProcess's records here. Call once, after the application
    /// object exists.
    static void install();

    [[nodiscard]] const std::deque<GitProcess::LogRecord>& records() const { return records_; }
    void clear();

    static constexpr std::size_t kLimit = 500;

signals:
    void recorded(const gity::session::GitProcess::LogRecord& record);
    void cleared();

private:
    explicit CommandLog(QObject* parent);
    void append(const GitProcess::LogRecord& record);

    std::deque<GitProcess::LogRecord> records_;
};

} // namespace gity::session
