// gity-askpass — the program git runs when it needs a credential.
//
// git and ssh both ask by executing a helper with the prompt as its only
// argument and reading the answer from its stdout. A separate binary rather
// than a mode of the main application, because git runs it as a child process
// and expects it to exit as soon as it has an answer; a second instance of a
// running GUI is the wrong shape entirely.
//
// It stores nothing. Whether the answer is remembered is git's own credential
// helper's business — that is where the user has already configured what they
// want, and where the platform keychains are properly integrated. A GUI that
// quietly kept its own copy of a password would be both a surprise and a
// second place for it to leak from.
#include "ui/panels/AskpassDialog.h"
#include "ui/theme/Theme.h"

#include <QApplication>

#include <cstdio>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("gity-askpass"));
    gity::ui::Theme::applySavedVariant(&app);

    // git passes the prompt as the single argument. With none, there is
    // nothing to ask, and answering blind would be worse than failing.
    if (argc < 2) {
        return 1;
    }

    gity::ui::AskpassDialog dialog(QString::fromLocal8Bit(argv[1]));
    if (dialog.exec() != QDialog::Accepted) {
        // A non-zero exit is how a helper declines; git then fails the
        // operation rather than retrying forever.
        return 1;
    }

    // stdout, one line, and nothing else on it ever: git reads the first line
    // and treats it as the answer.
    const QByteArray answer = dialog.answer().toLocal8Bit();
    std::fwrite(answer.constData(), 1, static_cast<std::size_t>(answer.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
    return 0;
}
