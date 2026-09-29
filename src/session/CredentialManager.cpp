#include "CredentialManager.h"

#include "core/git/Credential.h"
#include "session/CredentialInventory.h"
#include "session/GitProcess.h"

#include <QThread>

#include <functional>
#include <memory>

namespace gity::session {
namespace {

/// A lookup must answer from what is stored, never by asking. An empty
/// GIT_ASKPASS stops git's fallback chain (it would otherwise reach Gity's
/// own askpass helper and pop a sign-in dialog); GCM_INTERACTIVE does the same
/// for Git Credential Manager, which prompts on its own account.
const QList<GitProcess::EnvOverride>& quiet() {
    static const QList<GitProcess::EnvOverride> overrides{
        {QStringLiteral("GIT_ASKPASS"), QString()},
        {QStringLiteral("SSH_ASKPASS"), QString()},
        {QStringLiteral("GCM_INTERACTIVE"), QStringLiteral("never")},
    };
    return overrides;
}

/// The request for `url`, with the path always included: git drops it itself
/// unless credential.useHttpPath asks for it, so the decision stays git's.
bool requestFor(const QString& url, const QString& username, const QString& password,
                git::Credential* out) {
    git::Credential credential;
    if (!git::credentialForUrl(url.toStdString(), true, &credential)) {
        return false;
    }
    if (!username.isEmpty()) {
        credential.username = username.toStdString();
    }
    credential.password = password.toStdString();
    *out = std::move(credential);
    return true;
}

/// Runs `work` on its own thread and `done` back on `context`'s thread. The
/// thread cleans itself up; if `context` is gone by then, `done` is dropped.
void runAsync(QObject* context, std::function<void()> work, std::function<void()> done) {
    QThread* thread = QThread::create(std::move(work));
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    QObject::connect(thread, &QThread::finished, context, std::move(done));
    thread->start();
}

} // namespace

CredentialManager::CredentialManager(QObject* parent) : QObject(parent) {}

bool CredentialManager::usesCredentials(const QString& url) {
    return git::credentialForUrl(url.toStdString(), false, nullptr);
}

QStringList CredentialManager::configuredHelpers(const QString& workdir) {
    // Every credential.*helper key, so URL-scoped helpers show as well as the
    // global one. Read from the repository, so its own config is included.
    const GitResult result = GitProcess::run(
        workdir.isEmpty() ? QStringLiteral(".") : workdir,
        {QStringLiteral("config"), QStringLiteral("--show-scope"), QStringLiteral("--get-regexp"),
         QStringLiteral("^credential\\..*helper$")},
        15000);
    QStringList helpers;
    for (const QString& line : result.output.split(QChar('\n'), Qt::SkipEmptyParts)) {
        helpers << line.trimmed();
    }
    return helpers;
}

void CredentialManager::lookup(const QString& workdir, const QString& url) {
    git::Credential request;
    if (!requestFor(url, {}, {}, &request)) {
        emit lookedUp(url, false, {}, {}, tr("This remote does not use stored credentials."));
        return;
    }

    auto result = std::make_shared<GitResult>();
    auto hint = std::make_shared<QString>();
    const QByteArray input = QByteArray::fromStdString(git::serializeCredential(request));
    runAsync(
        this,
        [result, hint, workdir, input, url] {
            *result = GitProcess::runWithInputAndEnv(
                workdir,
                {QStringLiteral("-c"), QStringLiteral("credential.interactive=false"),
                 QStringLiteral("credential"), QStringLiteral("fill")},
                input, quiet(), 60000);
            // Nothing came back: say why, when the reason is a user GitHub CLI
            // will not answer for.
            if (!result->ok()) {
                *hint = CredentialInventory::accountHint(url);
            }
        },
        [this, result, hint, url] {
            if (!result->ok()) {
                // Nothing stored is the ordinary reason, and git words it as a
                // failure to prompt. Only a helper that could not even run is
                // worth passing on.
                const QString line = result->firstProblemLine();
                // The wording varies by version: "could not read Username",
                // "terminal prompts disabled", and since credential.interactive,
                // "unable to get password from user".
                const bool nothingStored = line.contains(QStringLiteral("could not read")) ||
                                           line.contains(QStringLiteral("prompts disabled")) ||
                                           line.contains(QStringLiteral("unable to get"));
                emit lookedUp(url, false, {}, {},
                              !hint->isEmpty() ? *hint : nothingStored ? QString() : line);
                return;
            }
            const git::Credential answer = git::parseCredential(result->output.toStdString());
            emit lookedUp(url, answer.hasSecret(), QString::fromStdString(answer.username),
                          QString::fromStdString(answer.password), {});
        });
}

void CredentialManager::store(const QString& workdir, const QString& url,
                              const QString& username, const QString& password,
                              const QString& previousUsername,
                              const QString& previousPassword) {
    git::Credential record;
    git::Credential previous;
    if (!requestFor(url, username, password, &record) ||
        !requestFor(url, previousUsername, previousPassword, &previous)) {
        emit finished(false, tr("This remote does not use stored credentials."));
        return;
    }
    if (!git::isWritable(record) || username.isEmpty() || password.isEmpty()) {
        emit finished(false, tr("A username and a password are both needed, on one line each."));
        return;
    }
    const bool renamed = !previousUsername.isEmpty() && previousUsername != username;

    auto result = std::make_shared<GitResult>();
    const QByteArray approve = QByteArray::fromStdString(git::serializeCredential(record));
    const QByteArray reject = QByteArray::fromStdString(git::serializeCredential(previous));
    runAsync(
        this,
        [result, workdir, approve, reject, renamed] {
            if (renamed) {
                static_cast<void>(GitProcess::runWithInputAndEnv(
                    workdir, {QStringLiteral("credential"), QStringLiteral("reject")}, reject,
                    quiet()));
            }
            *result = GitProcess::runWithInputAndEnv(
                workdir, {QStringLiteral("credential"), QStringLiteral("approve")}, approve,
                quiet());
        },
        [this, result] {
            emit finished(result->ok(),
                          result->ok() ? tr("Saved to your credential helper.")
                                       : tr("The credential helper did not accept it: %1")
                                             .arg(result->firstProblemLine()));
        });
}

void CredentialManager::remove(const QString& workdir, const QString& url,
                               const QString& username, const QString& password) {
    git::Credential record;
    if (!requestFor(url, username, password, &record) || !git::isWritable(record)) {
        emit finished(false, tr("This remote does not use stored credentials."));
        return;
    }
    auto result = std::make_shared<GitResult>();
    const QByteArray input = QByteArray::fromStdString(git::serializeCredential(record));
    runAsync(
        this,
        [result, workdir, input] {
            *result = GitProcess::runWithInputAndEnv(
                workdir, {QStringLiteral("credential"), QStringLiteral("reject")}, input,
                quiet());
        },
        [this, result] {
            emit finished(result->ok(), result->ok()
                                            ? tr("Removed from your credential helper.")
                                            : tr("The credential helper could not remove it: %1")
                                                  .arg(result->firstProblemLine()));
        });
}

} // namespace gity::session
