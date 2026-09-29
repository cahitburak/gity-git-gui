// Shows and changes the credential git is actually using for a remote.
//
// Gity stores no credentials (ADR-010), and git has no way to *list* what a
// helper holds — only to ask it for the credential matching a URL. So this is
// a window onto that question: `git credential fill` for what the helper
// would supply, `approve` to store a new one, `reject` to erase it. Whatever
// helper the user configured — a keychain, libsecret, Git Credential Manager,
// the plain-text store — answers, and keeps the secret.
//
// Every call runs on a thread of its own: a helper may put up a keychain
// prompt, and waiting for a person on the UI thread freezes the window.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace gity::session {

class CredentialManager : public QObject {
    Q_OBJECT

public:
    explicit CredentialManager(QObject* parent = nullptr);

    /// Whether `url` authenticates through a credential helper at all. SSH
    /// uses keys, and local paths nothing.
    [[nodiscard]] static bool usesCredentials(const QString& url);

    /// The configured helpers, as `key value` lines — global, system and the
    /// repository's own, including ones scoped to a URL.
    [[nodiscard]] static QStringList configuredHelpers(const QString& workdir);

    /// Asks the helpers what they would supply for `url`, without letting
    /// anything prompt. Answers with lookedUp.
    void lookup(const QString& workdir, const QString& url);

    /// Stores `username`/`password` for `url`. When the username changes, the
    /// previous credential is erased first so the helper does not keep both.
    void store(const QString& workdir, const QString& url, const QString& username,
               const QString& password, const QString& previousUsername,
               const QString& previousPassword);

    /// Erases the credential stored for `url` and `username`.
    void remove(const QString& workdir, const QString& url, const QString& username,
                const QString& password);

signals:
    /// `found` false with an empty `problem` simply means nothing is stored.
    void lookedUp(const QString& url, bool found, const QString& username,
                  const QString& password, const QString& problem);
    void finished(bool ok, const QString& message);
};

} // namespace gity::session
