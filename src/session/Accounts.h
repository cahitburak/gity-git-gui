// The accounts Gity knows about.
//
// **No secrets are stored here, and none ever will be.** ADR-010 settles that:
// the token lives in git's credential helper, in the platform keychain the
// user already configured, where git on the command line can reach it too. All
// this holds is what is needed to *name* an account — provider, host, username
// — which is public information already sitting in a remote URL.
//
// That is not a limitation to work around later. It is the reason signing in
// as a second account does not overwrite the first: git keys credentials by
// protocol, host **and** username, so binding a repository to an account is a
// matter of putting the username in its remote URL rather than of this client
// remembering a password.
#pragma once

#include "core/git/Provider.h"

#include <QList>
#include <QString>

namespace gity::session {

struct Account {
    git::Provider provider = git::Provider::Other;
    QString host;
    QString username;
    /// What the user calls it — "work", "personal". Optional.
    QString label;

    [[nodiscard]] bool valid() const { return !host.isEmpty() && !username.isEmpty(); }

    /// How git identifies this account: the same pair its credential helper
    /// keys on.
    [[nodiscard]] QString key() const { return username + QChar('@') + host; }

    [[nodiscard]] bool operator==(const Account& other) const {
        return host == other.host && username == other.username;
    }
};

class Accounts {
public:
    [[nodiscard]] static QList<Account> load();
    static void save(const QList<Account>& accounts);

    /// Adds `account`, replacing any entry with the same host and username.
    /// Returns the resulting list.
    static QList<Account> add(const Account& account);
    static QList<Account> remove(const Account& account);

    /// The accounts that could serve `remoteUrl`, i.e. those on its host.
    [[nodiscard]] static QList<Account> forRemote(const QString& remoteUrl);

    /// `remoteUrl` with `username` as its userinfo, which is how a repository
    /// is bound to one account when several exist for a host. Returns the URL
    /// unchanged when it has no host to attach a username to.
    [[nodiscard]] static QString remoteUrlAs(const QString& remoteUrl,
                                             const QString& username);
};

} // namespace gity::session
