// A throwaway repository for session tests, driven through GitProcess itself
// so the tests exercise the same door the application uses.
#pragma once

#include "core/git/Repository.h"
#include "session/GitProcess.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <git2.h>
#include <string>

namespace gity::test {

class TempRepo {
public:
    TempRepo() {
        EXPECT_TRUE(dir_.isValid());
        git({"init", "-q", "-b", "main"});
        // Line endings as written and no signing, whatever this machine's
        // config says: the tests compare bytes.
        git({"config", "core.autocrlf", "false"});
        git({"config", "commit.gpgsign", "false"});
        git({"config", "user.name", "Test"});
        git({"config", "user.email", "test@invalid"});
    }

    ~TempRepo() {
        if (repo_ != nullptr) {
            git_repository_free(repo_);
        }
    }

    TempRepo(const TempRepo&) = delete;
    TempRepo& operator=(const TempRepo&) = delete;

    [[nodiscard]] QString dir() const { return dir_.path(); }

    /// libgit2's view of it, reopened each time so it never caches an index
    /// git has since rewritten.
    [[nodiscard]] git_repository* repository() {
        if (repo_ != nullptr) {
            git_repository_free(repo_);
            repo_ = nullptr;
        }
        EXPECT_EQ(git_repository_open(&repo_, dir_.path().toUtf8().constData()), 0);
        return repo_;
    }

    session::GitResult git(const QStringList& args) const {
        const session::GitResult result = session::GitProcess::run(dir_.path(), args);
        EXPECT_TRUE(result.ok()) << "git " << args.join(' ').toStdString() << ": "
                                 << result.errorOutput.toStdString();
        return result;
    }

    void write(const QString& relative, const std::string& contents) const {
        const QString full = QDir(dir_.path()).filePath(relative);
        QDir().mkpath(QFileInfo(full).path());
        QFile file(full);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(contents.data(), static_cast<qint64>(contents.size()));
    }

    [[nodiscard]] std::string read(const QString& relative) const {
        QFile file(QDir(dir_.path()).filePath(relative));
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return file.readAll().toStdString();
    }

    void remove(const QString& relative) const {
        ASSERT_TRUE(QFile::remove(QDir(dir_.path()).filePath(relative)));
    }

    void commitAll(const QString& message = QStringLiteral("base")) const {
        git({"add", "-A"});
        git({"commit", "-q", "--allow-empty", "-m", message});
    }

    /// The staged contents of `path`, as bytes.
    [[nodiscard]] std::string indexContents(const QString& path) const {
        return git({"--literal-pathspecs", "show", ":" + path}).output.toStdString();
    }

    [[nodiscard]] bool inIndex(const QString& path) const {
        return !git({"--literal-pathspecs", "ls-files", "--", path}).output.trimmed().isEmpty();
    }

    [[nodiscard]] QString head() const { return git({"rev-parse", "HEAD"}).output.trimmed(); }

private:
    QTemporaryDir dir_;
    git_repository* repo_ = nullptr;
};

} // namespace gity::test
