#include "CommandLogView.h"

#include "session/CommandLog.h"
#include "ui/panels/Confirm.h"

#include <QCheckBox>
#include <QDir>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QVBoxLayout>

namespace gity::ui {
namespace {

/// Quoted the way a POSIX shell would need it, so a line can be copied from
/// the log and run.
QString shellQuoted(const QString& argument) {
    if (!argument.isEmpty() &&
        argument.indexOf(QRegularExpression(QStringLiteral("[^A-Za-z0-9_./:=@%+,-]"))) < 0) {
        return argument;
    }
    QString quoted = argument;
    quoted.replace(QChar('\''), QStringLiteral("'\\''"));
    return QStringLiteral("'%1'").arg(quoted);
}

} // namespace

CommandLogView::CommandLogView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 8);

    auto* bar = new QHBoxLayout;
    auto* heading = new QLabel(tr("Every git command Gity runs, as you could run it yourself."),
                               this);
    applyRole(heading, QStringLiteral("note"));
    bar->addWidget(heading, 1);
    internal_ = new QCheckBox(tr("Show bookkeeping"), this);
    internal_->setToolTip(tr("Also show the commands Gity runs for itself — the snapshots behind "
                             "Undo, and the like."));
    bar->addWidget(internal_);
    auto* clear = new QPushButton(tr("Clear"), this);
    bar->addWidget(clear);
    layout->addLayout(bar);

    text_ = new QPlainTextEdit(this);
    text_->setReadOnly(true);
    text_->setLineWrapMode(QPlainTextEdit::NoWrap);
    text_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text_->setMaximumBlockCount(4000);
    layout->addWidget(text_, 1);

    auto* log = session::CommandLog::instance();
    connect(log, &session::CommandLog::recorded, this, &CommandLogView::add);
    connect(log, &session::CommandLog::cleared, text_, &QPlainTextEdit::clear);
    connect(clear, &QPushButton::clicked, log, &session::CommandLog::clear);
    connect(internal_, &QCheckBox::toggled, this, &CommandLogView::rebuild);
    rebuild();
}

QString CommandLogView::format(const session::GitProcess::LogRecord& record) {
    QStringList words{QStringLiteral("git")};
    for (const QString& argument : record.args) {
        words << shellQuoted(argument);
    }
    const QString outcome =
        record.timedOut ? tr("stopped")
        : record.exitCode == 0
            ? QStringLiteral("✓")
            : tr("✕ exit %1").arg(record.exitCode);
    const QString seconds =
        record.elapsedMs < 1000 ? QStringLiteral("%1 ms").arg(record.elapsedMs)
                                : QStringLiteral("%1 s").arg(double(record.elapsedMs) / 1000.0,
                                                             0, 'f', 1);
    QString line = QStringLiteral("%1  [%2]  %3   %4 · %5")
                       .arg(record.started.toString(QStringLiteral("HH:mm:ss")),
                            QDir(record.workdir).dirName(), words.join(QChar(' ')), outcome,
                            seconds);
    // A failure's own words, a few lines of them, under the command.
    if (record.exitCode != 0 || record.timedOut) {
        const QStringList lines =
            record.errorOutput.split(QChar('\n'), Qt::SkipEmptyParts).mid(0, 4);
        for (const QString& detail : lines) {
            line += QStringLiteral("\n            %1").arg(detail.trimmed());
        }
    }
    return line;
}

void CommandLogView::add(const session::GitProcess::LogRecord& record) {
    if (record.internal && !internal_->isChecked()) {
        return;
    }
    QScrollBar* bar = text_->verticalScrollBar();
    const bool atEnd = bar->value() == bar->maximum();
    text_->appendPlainText(format(record));
    // Follow new commands only when already following; someone scrolled up
    // to read an earlier failure should not be pulled away from it.
    if (atEnd) {
        bar->setValue(bar->maximum());
    }
}

void CommandLogView::rebuild() {
    text_->clear();
    for (const auto& record : session::CommandLog::instance()->records()) {
        add(record);
    }
}

} // namespace gity::ui
