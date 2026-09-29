// ADR-003 — the single door every `git` invocation goes through.
//
// Shelling out is only safe if it is done the same way every time, so no
// caller builds its own command line. The contract this enforces:
//
//   * `git` is located once at startup and version-gated, never resolved from
//     PATH at call time;
//   * no terminal prompts, ever — a password prompt on a pipe nobody is
//     reading is an application that has hung;
//   * arguments go as a list, never a shell string, with `--` before every
//     pathspec;
//   * output is machine-readable and locale-independent;
//   * every invocation is bounded by a timeout, and abandoned when the
//     session that started it closes (see setThreadAbortFlag).
//
// It lives in session/ rather than core/ because it needs QProcess, and core
// stays Qt-free so the headless CI leg keeps proving the layering rule.
#pragma once

#include <QByteArray>
#include <QDateTime>

#include <atomic>
#include <functional>
#include <QString>
#include <QStringList>

namespace gity::session {

struct GitResult {
    int exitCode = -1;
    QString output;
    QString errorOutput;
    bool started = false;
    bool timedOut = false;

    [[nodiscard]] bool ok() const noexcept { return started && !timedOut && exitCode == 0; }

    /// The line most worth showing a user: git puts its real complaint on
    /// stderr, but a few commands explain themselves on stdout instead.
    [[nodiscard]] QString firstProblemLine() const;
};

class GitProcess {
public:
    /// Locates git and checks its version. Call once at startup; a missing or
    /// ancient git is a clear error now rather than a confusing one later.
    static bool locate(QString* version, QString* error);

    [[nodiscard]] static bool available() noexcept;
    [[nodiscard]] static QString version();
    [[nodiscard]] static QString executable();

    /// Runs git in `workdir`. `args` excludes the program name and any -C.
    [[nodiscard]] static GitResult run(const QString& workdir, const QStringList& args,
                                       int timeoutMs = 120000);

    /// Called with each complete line git writes to stderr while it works.
    /// Runs on the calling thread, which for every current caller is the
    /// session worker — so implementations must not touch UI directly.
    using ProgressCallback = std::function<void(const QString& line)>;

    /// As run(), reporting progress as it arrives rather than at the end.
    ///
    /// Network verbs run for minutes on a real repository, and a client that
    /// shows nothing until they finish is indistinguishable from one that has
    /// hung. `--progress` has to be in `args` for git to write anything when
    /// stderr is a pipe rather than a terminal.
    [[nodiscard]] static GitResult runStreaming(const QString& workdir, const QStringList& args,
                                                const ProgressCallback& onProgress,
                                                int timeoutMs = 600000);

    /// As run(), but git is started *in* `parentDir` without -C, for verbs
    /// whose working directory does not exist yet. Clone is the only one.
    [[nodiscard]] static GitResult runInParent(const QString& parentDir, const QStringList& args,
                                               const ProgressCallback& onProgress,
                                               int timeoutMs = 3600000);

    /// One extra environment variable for a single invocation.
    struct EnvOverride {
        QString name;
        QString value;
    };

    /// As run(), with additional environment variables.
    ///
    /// Needed for the variables that exist to replace an interactive editor —
    /// GIT_SEQUENCE_EDITOR and GIT_EDITOR — which are the only way to drive
    /// `rebase -i` from a GUI. They are per-invocation rather than part of the
    /// standard environment: setting them globally would silently change what
    /// every other command does when it wants a message.
    [[nodiscard]] static GitResult runWithEnv(const QString& workdir, const QStringList& args,
                                              const QList<EnvOverride>& overrides,
                                              int timeoutMs = 600000);

    /// As run(), with `input` written to git's stdin and the stream closed.
    /// Used for commit messages, which must not go through a command line.
    [[nodiscard]] static GitResult runWithInput(const QString& workdir, const QStringList& args,
                                                const QByteArray& input, int timeoutMs = 120000);

    /// Both: input on stdin and extra environment. `git credential` needs it,
    /// since a secret must not go through a command line and a lookup must
    /// not be allowed to prompt.
    [[nodiscard]] static GitResult runWithInputAndEnv(const QString& workdir,
                                                      const QStringList& args,
                                                      const QByteArray& input,
                                                      const QList<EnvOverride>& overrides,
                                                      int timeoutMs = 120000);

    /// Makes every invocation on the calling thread stop early once `flag`
    /// is set. A session worker registers its shutdown flag here, so closing
    /// a repository does not block the window until a ten-minute fetch or an
    /// hour-long clone finishes on its own. Null removes it.
    static void setThreadAbortFlag(const std::atomic<bool>* flag);

    /// One finished git invocation, for the command log. Never carries stdin
    /// — a commit message or a credential — and passwords in URL arguments
    /// are masked before it is built.
    struct LogRecord {
        QDateTime started;
        QString workdir;
        QStringList args;
        int exitCode = -1;
        bool timedOut = false;
        qint64 elapsedMs = 0;
        /// Bookkeeping the user did not ask for — undo snapshots and the
        /// like — so the log can keep it out of the way.
        bool internal = false;
        QString errorOutput;
    };
    using LogSink = std::function<void(const LogRecord&)>;

    /// Receives every invocation, on whichever thread ran it.
    static void setLogSink(LogSink sink);

    /// While one of these lives on a thread, that thread's invocations are
    /// marked internal.
    class InternalScope {
    public:
        InternalScope();
        ~InternalScope();
        InternalScope(const InternalScope&) = delete;
        InternalScope& operator=(const InternalScope&) = delete;
    };

    /// The opposite, inside an InternalScope: a command the user would want to
    /// see even though it runs as part of bookkeeping — the fetch behind a
    /// pull preview.
    class VisibleScope {
    public:
        VisibleScope();
        ~VisibleScope();
        VisibleScope(const VisibleScope&) = delete;
        VisibleScope& operator=(const VisibleScope&) = delete;

    private:
        int saved_ = 0;
    };

    /// Minimum version we accept. 2.30 is where `--porcelain=v2` and the
    /// options this client relies on are all present and stable.
    static constexpr int kMinimumMajor = 2;
    static constexpr int kMinimumMinor = 30;
};

} // namespace gity::session
