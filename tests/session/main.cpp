// Session tests run real git, so they need what the application has at
// startup: a QCoreApplication (for applicationDirPath and QProcess), libgit2
// initialised, and git located and version-gated.
//
// The user's own git configuration is shut out. A global hook, a signing key
// or an autocrlf setting on the machine running the tests must not decide
// whether they pass.
#include "session/GitProcess.h"

#include <QCoreApplication>
#include <QTemporaryFile>

#include <gtest/gtest.h>

#include <git2.h>

int main(int argc, char** argv) {
    qputenv("GIT_CONFIG_NOSYSTEM", "1");
    QTemporaryFile emptyGlobal;
    if (emptyGlobal.open()) {
        qputenv("GIT_CONFIG_GLOBAL", emptyGlobal.fileName().toUtf8());
    }

    QCoreApplication app(argc, argv);
    git_libgit2_init();

    QString version;
    QString error;
    if (!gity::session::GitProcess::locate(&version, &error)) {
        std::fprintf(stderr, "git not available: %s\n", qPrintable(error));
        return 1;
    }

    ::testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();
    git_libgit2_shutdown();
    return result;
}
