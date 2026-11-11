// The GitProcess contract, checked against the real git binary.
#include "TempRepo.h"

#include "session/GitProcess.h"

#include <gtest/gtest.h>

#include <QMutex>

using gity::session::GitProcess;

namespace {

/// Captures every log record while it lives.
class LogCapture {
public:
    LogCapture() {
        GitProcess::setLogSink([this](const GitProcess::LogRecord& record) {
            const QMutexLocker lock(&mutex_);
            records_.push_back(record);
        });
    }
    ~LogCapture() { GitProcess::setLogSink({}); }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    [[nodiscard]] QString everything() {
        const QMutexLocker lock(&mutex_);
        QString all;
        for (const auto& record : records_) {
            all += record.args.join(' ') + '\n' + record.errorOutput + '\n';
        }
        return all;
    }

private:
    QMutex mutex_;
    std::vector<GitProcess::LogRecord> records_;
};

} // namespace

TEST(GitProcess, TokensInUrlsNeverReachTheLogOrTheError) {
    gity::test::TempRepo repo;
    LogCapture log;
    // Port 1 on loopback: refused at once, with the URL in git's complaint.
    const auto result = GitProcess::run(
        repo.dir(),
        {"ls-remote", "https://ghp_abcdefghij0123456789@127.0.0.1:1/o/r.git",
         "https://me:hunter2@127.0.0.1:1/o/r.git"},
        20000);
    EXPECT_FALSE(result.ok());

    const QString logged = log.everything();
    EXPECT_FALSE(logged.contains("ghp_abcdefghij0123456789")) << logged.toStdString();
    EXPECT_FALSE(logged.contains("hunter2")) << logged.toStdString();
    EXPECT_FALSE(result.errorOutput.contains("ghp_abcdefghij0123456789"));
    EXPECT_FALSE(result.errorOutput.contains("hunter2"));
    EXPECT_TRUE(logged.contains("https://***@127.0.0.1:1/o/r.git")) << logged.toStdString();
}

TEST(GitProcess, StdinIsEndOfFileWhenNothingIsSent) {
    // A command that reads stdin must see end-of-file, not wait on a pipe.
    gity::test::TempRepo repo;
    const auto result = GitProcess::run(repo.dir(), {"hash-object", "--stdin"}, 20000);
    ASSERT_TRUE(result.ok()) << result.errorOutput.toStdString();
    EXPECT_EQ(result.output.trimmed(), "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391"); // empty blob
}

TEST(GitProcess, ATimeoutIsReportedAsOne) {
    gity::test::TempRepo repo;
    // `git var` with an unsatisfiable editor would block; a hook is simpler:
    // a commit whose pre-commit hook sleeps past the deadline.
    repo.write(".git/hooks/pre-commit", "#!/bin/sh\nsleep 5\n");
    QFile::setPermissions(repo.dir() + "/.git/hooks/pre-commit",
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    const auto result =
        GitProcess::run(repo.dir(), {"commit", "--allow-empty", "-q", "-m", "x"}, 500);
    EXPECT_TRUE(result.timedOut);
    EXPECT_FALSE(result.ok());
}
