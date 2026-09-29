// M1 — read-only browser.
//
// Streams commits from the ADR-004 session thread into the custom-painted
// graph view, so the first rows appear while the walk is still running. M0
// measured a cold 1M-commit walk at ~4 s against a 1 s budget; streaming is
// how that budget is met.

#include "MainWindow.h"
#include "session/CommandLog.h"
#include "session/GitProcess.h"
#include "ui/theme/Theme.h"
#include "core/model/EnglishPlural.h"

#include <git2.h>

#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QCommandLineParser>
#include <QTimer>
#include <QTranslator>

namespace {

/// Supplies English plural forms for "%n file(s)"-style strings; see
/// core/model/EnglishPlural.h. Everything else falls through untranslated.
class EnglishPlurals : public QTranslator {
public:
    using QTranslator::QTranslator;
    [[nodiscard]] bool isEmpty() const override { return false; }
    QString translate(const char* /*context*/, const char* sourceText,
                      const char* /*disambiguation*/, int n) const override {
        if (n < 0 || sourceText == nullptr) {
            return {};
        }
        return QString::fromStdString(gity::model::englishPlural(sourceText, n));
    }
};

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Gity"));
    QCoreApplication::setApplicationVersion(QStringLiteral(GITY_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("Gity"));
    // From the theme resource; the installed copy under share/icons serves the
    // desktop's launcher, this one the window and taskbar.
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/gity/theme/icon.svg")));
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.cahitburak.gity_git_gui"));
    EnglishPlurals plurals;
    QCoreApplication::installTranslator(&plurals);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("main", "A Git client."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("repository"),
        QCoreApplication::translate("main", "Repository to open on startup."));

    // Development aid: render the window to a file and exit. Lets the painted
    // views be inspected without a human at the keyboard, and gives CI a way
    // to catch rendering regressions later.
    const QCommandLineOption screenshotOption(
        QStringList{QStringLiteral("screenshot")},
        QCoreApplication::translate("main", "Render to <file> after loading, then exit."),
        QStringLiteral("file"));
    const QCommandLineOption changesTabOption(
        QStringList{QStringLiteral("changes")},
        QCoreApplication::translate("main", "Open on the Changes tab."));
    parser.addOption(changesTabOption);
    const QCommandLineOption imageTabOption(
        QStringList{QStringLiteral("image")},
        QCoreApplication::translate("main", "Open on the Image tab for <path>."),
        QStringLiteral("path"));
    parser.addOption(imageTabOption);
    parser.addOption(screenshotOption);
    parser.process(app);

    git_libgit2_init();
    // Before anything runs git, so the log has the whole session.
    gity::session::CommandLog::install();
    // The saved appearance, applied before the window exists so nothing is
    // painted twice.
    gity::ui::Theme::applySavedVariant(&app);

    // ADR-003 — locate git once, up front. A missing or ancient git is a clear
    // error now rather than a confusing failure at the first push.
    QString gitVersion;
    QString gitError;
    if (!gity::session::GitProcess::locate(&gitVersion, &gitError)) {
        qWarning("git unavailable: %s", qPrintable(gitError));
    }

    // Scoped, so the window — and the repository handles its worker holds —
    // are destroyed *before* git_libgit2_shutdown below. As a plain local it
    // outlived the shutdown, and freeing a git_repository after libgit2 has
    // been shut down is undefined rather than merely untidy. The symptom that
    // led here was 240 bytes the sanitizer reported at exit.
    int result = 0;
    {
        gity::app::MainWindow window;
        window.show();

        // Said to the user, not only to a log they will never read: without
        // git every write and every network verb fails, and the failures would
        // otherwise be the first they heard of it.
        if (!gitError.isEmpty()) {
            QMessageBox::warning(&window, QObject::tr("Git is not available"),
                                 QObject::tr("%1\n\nHistory can still be browsed, but "
                                             "committing, branching and every network "
                                             "operation need it.")
                                     .arg(gitError));
        }

        // Tabs come back as they were left, unless this is a screenshot run:
        // that renders exactly what it was given and must not touch — or
        // save over — the user's own session.
        const QStringList args = parser.positionalArguments();
        const bool screenshotRun = parser.isSet(screenshotOption);
        if (!screenshotRun) {
            window.setSessionPersistence(true);
            window.restoreSession(args.isEmpty());
        }
        if (!args.isEmpty()) {
            window.openRepository(args.first());
        }
        if (parser.isSet(changesTabOption)) {
            window.showChangesTab();
        }
        if (parser.isSet(imageTabOption)) {
            window.showImageTab(parser.value(imageTabOption));
        }
        if (parser.isSet(screenshotOption)) {
            const QString file = parser.value(screenshotOption);
            QTimer::singleShot(2000, &window, [&window, file] {
                if (!window.grab().save(file)) {
                    qWarning("could not write %s", qPrintable(file));
                }
                QCoreApplication::quit();
            });
        }

        result = QApplication::exec();
    }

    git_libgit2_shutdown();
    return result;
}
