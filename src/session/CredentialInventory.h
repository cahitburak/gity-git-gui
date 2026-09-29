// Every credential git could use on this computer, from every store that can
// be listed.
//
// git's own protocol cannot list (see CredentialManager), so each store is
// read in its own way, and acted on with its own tool — the same programs git
// runs, so nothing here is a second implementation of a helper:
//
//   Keyring      the desktop's Secret Service (libsecret), over D-Bus. Only
//                the attributes are read; secrets stay locked until asked.
//                Shown with `git credential-libsecret get`, removed with the
//                keyring's own Delete.
//   File         git's plain-text store, ~/.git-credentials and the XDG one.
//                Shown and removed with `git credential-store`.
//   GitHub CLI   accounts `gh auth status` reports. Shown with `gh auth token`;
//                the active one is changed with `gh auth switch`.
//
// Gity still keeps nothing (ADR-010). Every call runs off the UI thread.
#pragma once

#include <QList>
#include <QObject>
#include <QString>

namespace gity::session {

struct StoredCredential {
    enum class Source { Keyring, File, GitHubCli };
    Source source = Source::Keyring;
    QString protocol;
    QString host;
    QString path;
    QString username;
    /// Keyring item path, store file, or gh's storage — where it lives.
    QString location;
    /// GitHub CLI: the account gh hands git for this host.
    bool active = false;

    [[nodiscard]] QString url() const;
};

class CredentialInventory : public QObject {
    Q_OBJECT

public:
    explicit CredentialInventory(QObject* parent = nullptr);

    /// Gathers from every store; answers with listed.
    void list();
    /// The same, synchronously — for a thread that is already off the UI one.
    [[nodiscard]] static QList<StoredCredential> gather(QStringList* notes = nullptr);
    /// Fetches one secret, from its own store. Answers with revealed.
    void reveal(const StoredCredential& credential);
    /// Deletes one from its store. Answers with finished.
    void remove(const StoredCredential& credential);
    /// Makes `credential` the GitHub CLI account git gets for its host.
    void makeActive(const StoredCredential& credential);

    /// When GitHub CLI answers for `remoteUrl`'s host but will give git
    /// nothing because the URL names a different user, the sentence that says
    /// so and how to fix it; otherwise empty. Runs `gh auth status`, so call it
    /// off the UI thread — it is meant for explaining a failure.
    [[nodiscard]] static QString accountHint(const QString& remoteUrl);

signals:
    /// `notes` says which stores could not be read, and why.
    void listed(const QList<gity::session::StoredCredential>& credentials, const QStringList& notes);
    void revealed(const QString& secret, const QString& problem);
    void finished(bool ok, const QString& message);
};

} // namespace gity::session
