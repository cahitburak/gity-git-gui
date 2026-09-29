#include "CommandLog.h"

#include <QCoreApplication>
#include <QPointer>

namespace gity::session {

CommandLog::CommandLog(QObject* parent) : QObject(parent) {}

CommandLog* CommandLog::instance() {
    static QPointer<CommandLog> log;
    if (log.isNull()) {
        log = new CommandLog(QCoreApplication::instance());
    }
    return log.data();
}

void CommandLog::install() {
    CommandLog* log = instance();
    GitProcess::setLogSink([log](const GitProcess::LogRecord& record) {
        // Whichever thread ran git, the record is handed to the UI thread.
        QMetaObject::invokeMethod(log, [log, record] { log->append(record); },
                                  Qt::QueuedConnection);
    });
}

void CommandLog::append(const GitProcess::LogRecord& record) {
    records_.push_back(record);
    while (records_.size() > kLimit) {
        records_.pop_front();
    }
    emit recorded(record);
}

void CommandLog::clear() {
    records_.clear();
    emit cleared();
}

} // namespace gity::session
