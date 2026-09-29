#include "CredentialInventory.h"

#include "core/git/Credential.h"
#include "core/git/CredentialSources.h"
#include "session/GitProcess.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>

#ifdef GITY_HAVE_DBUS
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#endif

#include <functional>
#include <memory>

namespace gity::session {
namespace {

using Source = StoredCredential::Source;
using StringMap = QMap<QString, QString>;

void runAsync(QObject* context, std::function<void()> work, std::function<void()> done) {
    QThread* thread = QThread::create(std::move(work));
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    QObject::connect(thread, &QThread::finished, context, std::move(done));
    thread->start();
}

QStringList storeFiles() {
    // Where `git credential-store` looks when given no --file.
    QStringList files{QDir::home().filePath(QStringLiteral(".git-credentials"))};
    const QString xdg = qEnvironmentVariable("XDG_CONFIG_HOME");
    files << QDir(xdg.isEmpty() ? QDir::home().filePath(QStringLiteral(".config")) : xdg)
                 .filePath(QStringLiteral("git/credentials"));
    return files;
}

struct ProcessResult {
    bool ok = false;
    QString output;
    QString error;
};

/// A program other than git — gh. Found once on PATH, never through a shell.
ProcessResult runTool(const QString& program, const QStringList& args) {
    ProcessResult result;
    const QString path = QStandardPaths::findExecutable(program);
    if (path.isEmpty()) {
        result.error = QObject::tr("%1 is not installed").arg(program);
        return result;
    }
    QProcess process;
    process.setStandardInputFile(QProcess::nullDevice());
    process.start(path, args);
    if (!process.waitForFinished(30000)) {
        process.kill();
        result.error = QObject::tr("%1 did not answer").arg(program);
        return result;
    }
    result.ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    result.output = QString::fromUtf8(process.readAllStandardOutput());
    result.error = QString::fromUtf8(process.readAllStandardError()).trimmed();
    return result;
}

#ifdef GITY_HAVE_DBUS
const QString kSecrets = QStringLiteral("org.freedesktop.secrets");

/// git-credential-libsecret's items, by their attributes. Reading attributes
/// needs no unlock and returns no secret.
QList<StoredCredential> keyringCredentials(QString* problem) {
    qDBusRegisterMetaType<StringMap>();
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        *problem = QObject::tr("the desktop keyring is not reachable");
        return {};
    }
    QDBusMessage search = QDBusMessage::createMethodCall(
        kSecrets, QStringLiteral("/org/freedesktop/secrets"),
        QStringLiteral("org.freedesktop.Secret.Service"), QStringLiteral("SearchItems"));
    search << QVariant::fromValue(StringMap{{QStringLiteral("xdg:schema"),
                                             QStringLiteral("org.git.Password")}});
    const QDBusMessage found = bus.call(search, QDBus::Block, 10000);
    if (found.type() != QDBusMessage::ReplyMessage || found.arguments().size() < 2) {
        *problem = QObject::tr("the desktop keyring did not answer");
        return {};
    }
    QList<QDBusObjectPath> paths;
    for (int i = 0; i < 2; ++i) { // unlocked, then locked
        QList<QDBusObjectPath> part;
        found.arguments().at(i).value<QDBusArgument>() >> part;
        paths += part;
    }

    QList<StoredCredential> credentials;
    for (const QDBusObjectPath& path : paths) {
        QDBusMessage get = QDBusMessage::createMethodCall(
            kSecrets, path.path(), QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("Get"));
        get << QStringLiteral("org.freedesktop.Secret.Item") << QStringLiteral("Attributes");
        const QDBusMessage reply = bus.call(get, QDBus::Block, 10000);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            continue;
        }
        StringMap attributes;
        reply.arguments().at(0).value<QDBusVariant>().variant().value<QDBusArgument>() >>
            attributes;
        StoredCredential credential;
        credential.source = Source::Keyring;
        credential.protocol = attributes.value(QStringLiteral("protocol"));
        credential.host = attributes.value(QStringLiteral("server"));
        const QString port = attributes.value(QStringLiteral("port"));
        if (!port.isEmpty() && port != QStringLiteral("0")) {
            credential.host += QChar(':') + port;
        }
        credential.path = attributes.value(QStringLiteral("object"));
        credential.username = attributes.value(QStringLiteral("user"));
        credential.location = path.path();
        if (!credential.host.isEmpty()) {
            credentials << credential;
        }
    }
    return credentials;
}

bool deleteKeyringItem(const QString& itemPath, QString* problem) {
    QDBusMessage call = QDBusMessage::createMethodCall(
        kSecrets, itemPath, QStringLiteral("org.freedesktop.Secret.Item"),
        QStringLiteral("Delete"));
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 30000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        *problem = reply.errorMessage();
        return false;
    }
    // A prompt path other than "/" means the keyring wants to ask first.
    const QString prompt = reply.arguments().value(0).value<QDBusObjectPath>().path();
    if (!prompt.isEmpty() && prompt != QStringLiteral("/")) {
        *problem = QObject::tr("the keyring asked for confirmation; unlock it and try again");
        return false;
    }
    return true;
}
#endif

QByteArray requestFor(const StoredCredential& credential) {
    git::Credential request;
    request.protocol = credential.protocol.toStdString();
    request.host = credential.host.toStdString();
    request.path = credential.path.toStdString();
    request.username = credential.username.toStdString();
    return QByteArray::fromStdString(git::serializeCredential(request));
}

} // namespace

QString StoredCredential::url() const {
    QString text = QStringLiteral("%1://").arg(protocol.isEmpty() ? QStringLiteral("https") : protocol);
    if (!username.isEmpty()) {
        text += username + QChar('@');
    }
    text += host;
    if (!path.isEmpty()) {
        text += QChar('/') + path;
    }
    return text;
}

CredentialInventory::CredentialInventory(QObject* parent) : QObject(parent) {}

QString CredentialInventory::accountHint(const QString& remoteUrl) {
    if (!remoteUrl.contains(QChar('@')) || QStandardPaths::findExecutable(QStringLiteral("gh")).isEmpty()) {
        return {};
    }
    const ProcessResult gh =
        runTool(QStringLiteral("gh"), {QStringLiteral("auth"), QStringLiteral("status")});
    const git::AccountMismatch mismatch = git::findAccountMismatch(
        remoteUrl.toStdString(),
        git::parseGhAuthStatus((gh.output + QChar('\n') + gh.error).toStdString()));
    const QString user = QString::fromStdString(mismatch.urlUser);
    const QString active = QString::fromStdString(mismatch.activeAccount);
    switch (mismatch.kind) {
    case git::AccountMismatch::Kind::UnknownUser:
        return tr("This remote's address names the user \u201c%1\u201d, but GitHub CLI is signed "
                  "in as \u201c%2\u201d and only answers for that account — so git got no token, "
                  "fell back to a password, and GitHub refuses passwords. Change the user in the "
                  "address to %2, or remove it: Repository \u25b8 Remotes \u25b8 Change URL.")
            .arg(user, active);
    case git::AccountMismatch::Kind::InactiveUser:
        return tr("This remote's address names \u201c%1\u201d. GitHub CLI holds that account but is "
                  "using \u201c%2\u201d, and only answers for the one in use. Make %1 the one in "
                  "use under Tools \u25b8 Credentials (Use for git), or remove the user from the "
                  "address so whichever account is in use answers.")
            .arg(user, active);
    case git::AccountMismatch::Kind::None:
        break;
    }
    return {};
}

QList<StoredCredential> CredentialInventory::gather(QStringList* notes) {
    QList<StoredCredential> credentials;
#ifdef GITY_HAVE_DBUS
    QString problem;
    credentials += keyringCredentials(&problem);
    if (!problem.isEmpty() && notes != nullptr) {
        *notes << problem;
    }
#endif
    for (const QString& file : storeFiles()) {
        QFile store(file);
        if (!store.open(QIODevice::ReadOnly)) {
            continue;
        }
        for (const git::StoredEntry& entry :
             git::parseCredentialStore(store.readAll().toStdString())) {
            StoredCredential credential;
            credential.source = Source::File;
            credential.protocol = QString::fromStdString(entry.protocol);
            credential.host = QString::fromStdString(entry.host);
            credential.path = QString::fromStdString(entry.path);
            credential.username = QString::fromStdString(entry.username);
            credential.location = file;
            credentials << credential;
        }
    }
    const ProcessResult gh =
        runTool(QStringLiteral("gh"), {QStringLiteral("auth"), QStringLiteral("status")});
    // gh writes its status to stderr on some versions and stdout on others,
    // and exits 1 when any account needs attention; read both.
    for (const git::GhAccount& account :
         git::parseGhAuthStatus((gh.output + QChar('\n') + gh.error).toStdString())) {
        StoredCredential credential;
        credential.source = Source::GitHubCli;
        credential.protocol = QStringLiteral("https");
        credential.host = QString::fromStdString(account.host);
        credential.username = QString::fromStdString(account.account);
        credential.location = QString::fromStdString(account.storage);
        credential.active = account.active;
        credentials << credential;
    }
    return credentials;
}

void CredentialInventory::list() {
    auto credentials = std::make_shared<QList<StoredCredential>>();
    auto notes = std::make_shared<QStringList>();
    runAsync(
        this, [credentials, notes] { *credentials = gather(notes.get()); },
        [this, credentials, notes] { emit listed(*credentials, *notes); });
}

void CredentialInventory::reveal(const StoredCredential& credential) {
    auto secret = std::make_shared<QString>();
    auto problem = std::make_shared<QString>();
    runAsync(
        this,
        [credential, secret, problem] {
            if (credential.source == Source::GitHubCli) {
                const ProcessResult token = runTool(
                    QStringLiteral("gh"),
                    {QStringLiteral("auth"), QStringLiteral("token"), QStringLiteral("--hostname"),
                     credential.host, QStringLiteral("--user"), credential.username});
                *secret = token.output.trimmed();
                if (!token.ok) {
                    *problem = token.error;
                }
                return;
            }
            // The store's own helper, asked directly: going through
            // `git credential fill` would ask whichever helpers the config
            // names for this host, which may not include the one it is in.
            const QStringList helper =
                credential.source == Source::Keyring
                    ? QStringList{QStringLiteral("credential-libsecret"), QStringLiteral("get")}
                    : QStringList{QStringLiteral("credential-store"),
                                  QStringLiteral("--file=%1").arg(credential.location),
                                  QStringLiteral("get")};
            const GitResult result =
                GitProcess::runWithInput(QDir::homePath(), helper, requestFor(credential));
            *secret = QString::fromStdString(
                git::parseCredential(result.output.toStdString()).password);
            if (secret->isEmpty()) {
                *problem = result.ok() ? QObject::tr("The store returned no password.")
                                       : result.firstProblemLine();
            }
        },
        [this, secret, problem] { emit revealed(*secret, *problem); });
}

void CredentialInventory::remove(const StoredCredential& credential) {
    auto ok = std::make_shared<bool>(false);
    auto problem = std::make_shared<QString>();
    runAsync(
        this,
        [credential, ok, problem] {
            switch (credential.source) {
            case Source::Keyring:
#ifdef GITY_HAVE_DBUS
                *ok = deleteKeyringItem(credential.location, problem.get());
#endif
                break;
            case Source::File: {
                const GitResult result = GitProcess::runWithInput(
                    QDir::homePath(),
                    {QStringLiteral("credential-store"),
                     QStringLiteral("--file=%1").arg(credential.location), QStringLiteral("erase")},
                    requestFor(credential));
                *ok = result.ok();
                *problem = result.firstProblemLine();
                break;
            }
            case Source::GitHubCli: {
                const ProcessResult logout = runTool(
                    QStringLiteral("gh"),
                    {QStringLiteral("auth"), QStringLiteral("logout"), QStringLiteral("--hostname"),
                     credential.host, QStringLiteral("--user"), credential.username});
                *ok = logout.ok;
                *problem = logout.error;
                break;
            }
            }
        },
        [this, ok, problem] {
            emit finished(*ok, *ok ? tr("Removed.") : tr("Could not remove it: %1").arg(*problem));
        });
}

void CredentialInventory::makeActive(const StoredCredential& credential) {
    auto result = std::make_shared<ProcessResult>();
    runAsync(
        this,
        [credential, result] {
            *result = runTool(QStringLiteral("gh"),
                              {QStringLiteral("auth"), QStringLiteral("switch"),
                               QStringLiteral("--hostname"), credential.host,
                               QStringLiteral("--user"), credential.username});
        },
        [this, result, credential] {
            emit finished(result->ok,
                          result->ok
                              ? tr("git now gets %1 for %2.").arg(credential.username, credential.host)
                              : tr("Could not switch: %1").arg(result->error));
        });
}

} // namespace gity::session
