#include "Accounts.h"

#include "core/git/RemoteUrl.h"

#include <QSettings>

namespace gity::session {
namespace {

constexpr auto kGroup = "accounts";

QSettings settings() {
    return QSettings(QStringLiteral("Gity"), QStringLiteral("Gity"));
}

/// The provider a stored name says, by providerName(); Other for none.
git::Provider providerNamed(const QString& name) {
    for (const git::Provider provider : git::allProviders()) {
        const std::string_view known = git::providerName(provider);
        if (name == QLatin1String(known.data(), static_cast<qsizetype>(known.size()))) {
            return provider;
        }
    }
    return git::Provider::Other;
}

} // namespace

QList<Account> Accounts::load() {
    QList<Account> accounts;
    QSettings store = settings();
    const int count = store.beginReadArray(QLatin1String(kGroup));
    for (int i = 0; i < count; ++i) {
        store.setArrayIndex(i);
        Account account;
        account.host = store.value(QStringLiteral("host")).toString();
        account.username = store.value(QStringLiteral("username")).toString();
        account.label = store.value(QStringLiteral("label")).toString();
        // Re-derived from the host where the host says, so better recognition
        // later still applies. Where it cannot — a self-hosted GitHub
        // Enterprise or Gitea on a plain name — the service chosen when the
        // account was added stands; without it browser sign-in was lost.
        account.provider = git::providerForHost(account.host.toStdString());
        if (account.provider == git::Provider::Other) {
            account.provider = providerNamed(store.value(QStringLiteral("provider")).toString());
        }
        if (account.valid()) {
            accounts.push_back(account);
        }
    }
    store.endArray();
    return accounts;
}

void Accounts::save(const QList<Account>& accounts) {
    QSettings store = settings();
    store.beginWriteArray(QLatin1String(kGroup));
    // Cleared first: beginWriteArray leaves entries beyond the new size in
    // place, so a removal would otherwise leave the last account behind.
    store.remove(QString());
    for (int i = 0; i < accounts.size(); ++i) {
        store.setArrayIndex(i);
        store.setValue(QStringLiteral("host"), accounts.at(i).host);
        store.setValue(QStringLiteral("username"), accounts.at(i).username);
        store.setValue(QStringLiteral("label"), accounts.at(i).label);
        const std::string_view provider = git::providerName(accounts.at(i).provider);
        store.setValue(
            QStringLiteral("provider"),
            QString::fromLatin1(provider.data(), static_cast<qsizetype>(provider.size())));
    }
    store.endArray();
    store.sync();
}

QList<Account> Accounts::add(const Account& account) {
    QList<Account> accounts = load();
    accounts.removeAll(account);
    accounts.push_back(account);
    save(accounts);
    return accounts;
}

QList<Account> Accounts::remove(const Account& account) {
    QList<Account> accounts = load();
    accounts.removeAll(account);
    save(accounts);
    return accounts;
}

QList<Account> Accounts::forRemote(const QString& remoteUrl) {
    const QString host = QString::fromStdString(git::hostOfRemote(remoteUrl.toStdString()));
    if (host.isEmpty()) {
        return {};
    }
    QList<Account> matching;
    for (const Account& account : load()) {
        if (account.host.compare(host, Qt::CaseInsensitive) == 0) {
            matching.push_back(account);
        }
    }
    return matching;
}

QString Accounts::remoteUrlAs(const QString& remoteUrl, const QString& username) {
    // The rule lives in core, where it is tested against the shapes a remote
    // URL actually arrives in.
    return QString::fromStdString(
        git::remoteUrlWithUser(remoteUrl.toStdString(), username.toStdString()));
}

} // namespace gity::session
