#include "GitProcess.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QDateTime>
#include <QElapsedTimer>
#include <QMutex>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

namespace gity::session {
namespace {

QString g_executable;
QString g_version;
bool g_available = false;

/// Where the askpass helper lives: next to the running executable, never
/// resolved from PATH. A credential prompt is the one dialog where running
/// whatever happens to be named `gity-askpass` on the user's PATH would be an
/// obvious way to hand someone's password to something else.
QString askpassHelperPath() {
    const QString directory = QCoreApplication::applicationDirPath();
    if (directory.isEmpty()) {
        return {};
    }
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("gity-askpass.exe");
#else
    const QString name = QStringLiteral("gity-askpass");
#endif

    // Beside the binary in a build tree and in a Linux install; inside the
    // bundle on macOS, where the app itself sits in Contents/MacOS.
    const QStringList candidates{directory + QChar('/') + name,
                                 directory + QStringLiteral("/../askpass/") + name};
    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isExecutable()) {
            return info.absoluteFilePath();
        }
    }
    return {};
}

/// The environment every invocation runs under.
QProcessEnvironment gitEnvironment() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // A prompt on a pipe nobody reads is an application that has hung. This
    // stays 0 even with the askpass helper in place: the helper is the only
    // route a prompt may take, and leaving the terminal route open would mean
    // a hang whenever the helper is missing rather than a clean failure.
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));

    const QString askpass = askpassHelperPath();
    if (!askpass.isEmpty()) {
        env.insert(QStringLiteral("GIT_ASKPASS"), askpass);
        // ssh has its own variable, and on a machine with no DISPLAY-bound
        // agent it ignores SSH_ASKPASS unless told the helper is required.
        env.insert(QStringLiteral("SSH_ASKPASS"), askpass);
        env.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force"));
    } else {
        // Better an explicit failure than git falling back to a helper the
        // user configured for a terminal and waiting on a prompt nobody sees.
        env.remove(QStringLiteral("GIT_ASKPASS"));
        env.remove(QStringLiteral("SSH_ASKPASS"));
    }

    // Machine-readable and locale-independent. Parsing localized git output is
    // how tools break for everyone outside en_US.
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    env.insert(QStringLiteral("LANG"), QStringLiteral("C"));

    // Read-only commands must not take the index lock: the user may have git
    // running in a terminal at the same time.
    env.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));

    // Never let a pager block a pipe.
    env.insert(QStringLiteral("GIT_PAGER"), QStringLiteral("cat"));
    env.insert(QStringLiteral("PAGER"), QStringLiteral("cat"));

    return env;
}

/// Drains stderr into `buffer` and hands each complete line to `onProgress`.
/// git separates progress updates with \r, not \n, so waiting for newlines
/// would deliver the whole transfer as one line after it had finished.
void drainProgress(QByteArray& buffer, const QByteArray& chunk,
                   const GitProcess::ProgressCallback& onProgress) {
    buffer += chunk;
    int start = 0;
    for (int i = 0; i < buffer.size(); ++i) {
        const char c = buffer.at(i);
        if (c != '\n' && c != '\r') {
            continue;
        }
        const QString line = QString::fromUtf8(buffer.mid(start, i - start)).trimmed();
        if (!line.isEmpty()) {
            onProgress(line);
        }
        start = i + 1;
    }
    buffer.remove(0, start);
}

/// The worker thread's abort flag, if it has one. Thread-local because the
/// flag belongs to whichever session's worker is running the command, and the
/// only thing git invocations share is the thread they block.
thread_local const std::atomic<bool>* t_abort = nullptr;

struct Request {
    QString workdir;
    QStringList args;
    /// False only for clone, whose working directory does not exist yet.
    bool useDashC = true;
    const QByteArray* input = nullptr;
    QList<GitProcess::EnvOverride> overrides;
    const GitProcess::ProgressCallback* onProgress = nullptr;
    int timeoutMs = 120000;
};

/// Stops `process`: terminate() first so git can remove its lock files, kill()
/// only if it will not go.
void stop(QProcess& process) {
    process.terminate();
    if (!process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished(1000);
    }
}

GitResult executeUnlogged(const Request& request) {
    GitResult result;
    if (!g_available) {
        result.errorOutput = QStringLiteral("git was not located at startup");
        return result;
    }

    QProcess process;
    QProcessEnvironment env = gitEnvironment();
    for (const GitProcess::EnvOverride& override : request.overrides) {
        env.insert(override.name, override.value);
    }
    process.setProcessEnvironment(env);
    process.setWorkingDirectory(request.workdir);
    // Anything that reads stdin gets end-of-file rather than a pipe that is
    // never written or closed. A hook that asks a question, or an editor git
    // decided to open, would otherwise wait on it until the timeout.
    if (request.input == nullptr) {
        process.setStandardInputFile(QProcess::nullDevice());
    }
    // Progress is on stderr, so that is the channel worth waiting on; stdout
    // is drained every pass regardless, because a full pipe would deadlock a
    // process nobody is reading from.
    process.setReadChannel(QProcess::StandardError);

    // Argument list, never a shell string: a path containing a space or a
    // semicolon must not become two arguments or two commands.
    QStringList full;
    if (request.useDashC) {
        full << QStringLiteral("-C") << request.workdir;
    }
    full += request.args;

    process.start(g_executable, full);
    if (!process.waitForStarted(5000)) {
        result.errorOutput = QStringLiteral("could not start git");
        return result;
    }
    result.started = true;

    if (request.input != nullptr) {
        process.write(*request.input);
        process.closeWriteChannel();
    }

    QElapsedTimer timer;
    timer.start();
    QByteArray standardOutput;
    QByteArray standardError;
    QByteArray pending;

    while (process.state() != QProcess::NotRunning) {
        // A short wait rather than the full timeout: the loop has to come back
        // around to drain output, to notice the deadline while git is quiet —
        // which it is for long stretches of a large clone — and to notice the
        // session closing, which must not wait ten minutes for a fetch.
        process.waitForReadyRead(100);

        standardOutput += process.readAllStandardOutput();
        const QByteArray chunk = process.readAllStandardError();
        standardError += chunk;
        if (!chunk.isEmpty() && request.onProgress != nullptr && *request.onProgress) {
            drainProgress(pending, chunk, *request.onProgress);
        }

        if (t_abort != nullptr && t_abort->load(std::memory_order_relaxed)) {
            stop(process);
            result.errorOutput = QStringLiteral("git was stopped because the repository closed");
            result.timedOut = true;
            return result;
        }
        if (timer.elapsed() > request.timeoutMs) {
            result.timedOut = true;
            stop(process);
            result.errorOutput =
                QStringLiteral("git timed out after %1 ms").arg(request.timeoutMs);
            return result;
        }
    }

    // Whatever arrived between the last read and the process exiting.
    standardOutput += process.readAllStandardOutput();
    standardError += process.readAllStandardError();

    // A crash is a failure whatever the exit code says; on some platforms a
    // killed process reports 0.
    result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    result.output = QString::fromUtf8(standardOutput);
    result.errorOutput = QString::fromUtf8(standardError);
    return result;
}

/// Where finished commands are reported; set once at startup.
QMutex g_sinkMutex;
GitProcess::LogSink g_sink;
thread_local int t_internalDepth = 0;

/// A password in a URL argument is replaced before the command is shown to
/// anyone: `clone https://me:secret@host/...` must not reach the log.
QString redact(const QString& argument) {
    static const QRegularExpression userinfo(QStringLiteral("(://[^/:@\\s]+):[^@/\\s]+@"));
    QString shown = argument;
    shown.replace(userinfo, QStringLiteral("\\1:***@"));
    return shown;
}

GitResult execute(const Request& request) {
    QElapsedTimer timer;
    timer.start();
    const QDateTime started = QDateTime::currentDateTime();
    GitResult result = executeUnlogged(request);

    GitProcess::LogSink sink;
    {
        const QMutexLocker lock(&g_sinkMutex);
        sink = g_sink;
    }
    if (sink) {
        GitProcess::LogRecord record;
        record.started = started;
        record.workdir = request.workdir;
        for (const QString& argument : request.args) {
            record.args << redact(argument);
        }
        record.exitCode = result.started ? result.exitCode : -1;
        record.timedOut = result.timedOut;
        record.elapsedMs = timer.elapsed();
        record.internal = t_internalDepth > 0;
        // stderr only, and bounded: stdout can be a whole log, and stdin —
        // commit messages, credentials — is never recorded at all.
        record.errorOutput = result.errorOutput.left(4000);
        sink(record);
    }
    return result;
}

} // namespace

void GitProcess::setLogSink(LogSink sink) {
    const QMutexLocker lock(&g_sinkMutex);
    g_sink = std::move(sink);
}

GitProcess::InternalScope::InternalScope() {
    ++t_internalDepth;
}

GitProcess::InternalScope::~InternalScope() {
    --t_internalDepth;
}

GitProcess::VisibleScope::VisibleScope() : saved_(t_internalDepth) {
    t_internalDepth = 0;
}

GitProcess::VisibleScope::~VisibleScope() {
    t_internalDepth = saved_;
}

QString GitResult::firstProblemLine() const {
    const QString source = errorOutput.trimmed().isEmpty() ? output : errorOutput;
    for (const QString& line : source.split(QChar('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            return trimmed;
        }
    }
    return {};
}

bool GitProcess::locate(QString* version, QString* error) {
    g_available = false;
    g_executable = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (g_executable.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("git was not found on PATH. Install Git and restart.");
        }
        return false;
    }

    QProcess process;
    process.setProcessEnvironment(gitEnvironment());
    process.start(g_executable, {QStringLiteral("--version")});
    if (!process.waitForFinished(5000) || process.exitCode() != 0) {
        if (error != nullptr) {
            *error = QStringLiteral("%1 did not report a version").arg(g_executable);
        }
        return false;
    }

    const QString reported = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    static const QRegularExpression pattern(QStringLiteral("(\\d+)\\.(\\d+)(?:\\.(\\d+))?"));
    const QRegularExpressionMatch match = pattern.match(reported);
    if (!match.hasMatch()) {
        if (error != nullptr) {
            *error = QStringLiteral("could not parse git version from \"%1\"").arg(reported);
        }
        return false;
    }

    const int major = match.captured(1).toInt();
    const int minor = match.captured(2).toInt();
    if (major < kMinimumMajor || (major == kMinimumMajor && minor < kMinimumMinor)) {
        if (error != nullptr) {
            *error = QStringLiteral("git %1.%2 is too old; %3.%4 or newer is required")
                         .arg(major)
                         .arg(minor)
                         .arg(kMinimumMajor)
                         .arg(kMinimumMinor);
        }
        return false;
    }

    g_version = match.captured(0);
    g_available = true;
    if (version != nullptr) {
        *version = g_version;
    }
    return true;
}

bool GitProcess::available() noexcept {
    return g_available;
}

QString GitProcess::version() {
    return g_version;
}

QString GitProcess::executable() {
    return g_executable;
}

void GitProcess::setThreadAbortFlag(const std::atomic<bool>* flag) {
    t_abort = flag;
}

GitResult GitProcess::run(const QString& workdir, const QStringList& args, int timeoutMs) {
    Request request;
    request.workdir = workdir;
    request.args = args;
    request.timeoutMs = timeoutMs;
    return execute(request);
}

GitResult GitProcess::runStreaming(const QString& workdir, const QStringList& args,
                                   const ProgressCallback& onProgress, int timeoutMs) {
    Request request;
    request.workdir = workdir;
    request.args = args;
    request.onProgress = &onProgress;
    request.timeoutMs = timeoutMs;
    return execute(request);
}

GitResult GitProcess::runInParent(const QString& parentDir, const QStringList& args,
                                  const ProgressCallback& onProgress, int timeoutMs) {
    Request request;
    request.workdir = parentDir;
    request.args = args;
    request.useDashC = false;
    request.onProgress = &onProgress;
    request.timeoutMs = timeoutMs;
    return execute(request);
}

GitResult GitProcess::runWithEnv(const QString& workdir, const QStringList& args,
                                 const QList<EnvOverride>& overrides, int timeoutMs) {
    Request request;
    request.workdir = workdir;
    request.args = args;
    request.overrides = overrides;
    request.timeoutMs = timeoutMs;
    return execute(request);
}

GitResult GitProcess::runWithInput(const QString& workdir, const QStringList& args,
                                   const QByteArray& input, int timeoutMs) {
    Request request;
    request.workdir = workdir;
    request.args = args;
    request.input = &input;
    request.timeoutMs = timeoutMs;
    return execute(request);
}

GitResult GitProcess::runWithInputAndEnv(const QString& workdir, const QStringList& args,
                                         const QByteArray& input,
                                         const QList<EnvOverride>& overrides, int timeoutMs) {
    Request request;
    request.workdir = workdir;
    request.args = args;
    request.input = &input;
    request.overrides = overrides;
    request.timeoutMs = timeoutMs;
    return execute(request);
}

} // namespace gity::session
