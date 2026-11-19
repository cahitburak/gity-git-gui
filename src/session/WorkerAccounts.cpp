// RepoWorker: which account a repository uses for its remote's host.
#include "RepoWorker.h"

#include "core/git/Provider.h"
#include "core/git/RepoAccount.h"
#include "core/git/Submodules.h"

#include "Background.h"
#include "CredentialInventory.h"

#include <QDir>

namespace gity::session {

namespace {

/// The https host a repository's remote is on, or empty for ssh and local
/// remotes, which choose their account some other way.
QString httpsHostOf(const std::vector<gity::git::RemoteEntry>& remotes) {
    const gity::git::RemoteEntry* chosen = nullptr;
    for (const auto& remote : remotes) {
        if (remote.name == "origin" || chosen == nullptr) {
            chosen = &remote;
        }
    }
    if (chosen == nullptr) {
        return {};
    }
    const QString url = QString::fromStdString(chosen->fetchUrl);
    if (!url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        return {};
    }
    return QString::fromStdString(gity::git::hostOfRemote(chosen->fetchUrl));
}

QString configValue(const QString& workdir, const QStringList& args) {
    return GitProcess::run(workdir, args).output.section(QChar('\n'), 0, 0).trimmed();
}

} // namespace

void RepoWorker::loadAccountChoice() {
    if (!repo_) {
        return;
    }
    const GitProcess::InternalScope internal;
    const QString host = httpsHostOf(gity::git::listRemotes(repo_.get()));
    if (host.isEmpty()) {
        emit accountChoiceReady({}, false, {}, {}, {});
        return;
    }
    const QString url = QStringLiteral("https://%1").arg(host);
    // Who answers for this host — GitHub CLI, or a keyring or store file.
    const QString helper =
        configValue(workdir_, {QStringLiteral("config"), QStringLiteral("--get-urlmatch"),
                               QStringLiteral("credential.helper"), url});
    const bool viaGh = helper.contains(QStringLiteral("gh auth"));

    // What this repository has chosen, in its own config.
    QString chosen;
    if (viaGh) {
        const GitResult helpers =
            GitProcess::run(workdir_, {QStringLiteral("config"), QStringLiteral("--local"),
                                       QStringLiteral("--get-all"),
                                       QStringLiteral("credential.%1.helper").arg(url)});
        for (const QString& line : helpers.output.split(QChar('\n'), Qt::SkipEmptyParts)) {
            const QString account =
                QString::fromStdString(gity::git::accountOfGhHelper(line.toStdString()));
            if (!account.isEmpty()) {
                chosen = account;
            }
        }
    } else {
        chosen = configValue(workdir_, {QStringLiteral("config"), QStringLiteral("--local"),
                                        QStringLiteral("--get"),
                                        QStringLiteral("credential.%1.username").arg(url)});
    }

    // The accounts known for the host come from the keyring and GitHub CLI,
    // which can take tens of seconds — a locked keyring waits on a person.
    // Gathered on a thread of their own, so this repository's worker is not
    // held up meanwhile; the answer is sent from here when it arrives.
    runInBackground<QList<StoredCredential>>(
        this, [] { return CredentialInventory::gather(); },
        [this, host, viaGh, chosen](QList<StoredCredential> stored) {
            QStringList accounts;
            QString defaultAccount;
            for (const StoredCredential& credential : stored) {
                if (credential.host.compare(host, Qt::CaseInsensitive) != 0 ||
                    (viaGh != (credential.source == StoredCredential::Source::GitHubCli))) {
                    continue;
                }
                if (!credential.username.isEmpty() && !accounts.contains(credential.username)) {
                    accounts << credential.username;
                }
                if (credential.active) {
                    defaultAccount = credential.username;
                }
            }
            emit accountChoiceReady(host, viaGh, accounts, defaultAccount, chosen);
        });
}

void RepoWorker::setAccountChoice(const QString& account) {
    const QString verb = QObject::tr("Account");
    if (!repo_) {
        return;
    }
    const QString host = httpsHostOf(gity::git::listRemotes(repo_.get()));
    if (host.isEmpty() ||
        (!account.isEmpty() && !gity::git::isSafeAccountName(account.toStdString()))) {
        emit historyEditFinished(
            verb, false, false,
            QObject::tr("%1 is not an account name git can be told to use.").arg(account));
        return;
    }
    const QString url = QStringLiteral("https://%1").arg(host);
    const bool viaGh =
        configValue(workdir_, {QStringLiteral("config"), QStringLiteral("--get-urlmatch"),
                               QStringLiteral("credential.helper"), url})
            .contains(QStringLiteral("gh auth"));

    // This repository, and every initialised submodule on the same host: a
    // submodule is a repository with its own config, and one left on the old
    // account is how a submodule fails after its parent works.
    QStringList targets{workdir_};
    for (const auto& submodule : gity::git::listSubmodules(repo_.get())) {
        if (!submodule.initialised) {
            continue;
        }
        const QString path = QDir(workdir_).filePath(QString::fromStdString(submodule.path));
        const QString subUrl = configValue(
            path, {QStringLiteral("remote"), QStringLiteral("get-url"), QStringLiteral("origin")});
        if (QString::fromStdString(gity::git::hostOfRemote(subUrl.toStdString()))
                    .compare(host, Qt::CaseInsensitive) == 0 &&
            subUrl.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
            targets << path;
        }
    }

    const QString helperKey = QStringLiteral("credential.%1.helper").arg(url);
    const QString userKey = QStringLiteral("credential.%1.username").arg(url);
    const QString helper = QString::fromStdString(
        gity::git::ghAccountHelper(host.toStdString(), account.toStdString()));
    QStringList failed;
    for (const QString& target : targets) {
        const auto set = [&target](const QStringList& args) {
            return GitProcess::run(
                target, QStringList{QStringLiteral("config"), QStringLiteral("--local")} + args);
        };
        // Clear what Gity set before, then set the new choice. Unsetting
        // something absent fails harmlessly, so its result is not checked.
        static_cast<void>(set({QStringLiteral("--unset-all"), helperKey}));
        static_cast<void>(set({QStringLiteral("--unset-all"), userKey}));
        if (account.isEmpty()) {
            continue;
        }
        bool ok = true;
        if (viaGh) {
            // An empty value first: it resets the list the global config
            // built, so GitHub CLI's own helper — which answers only for the
            // active account — is not asked before this one.
            ok = set({QStringLiteral("--add"), helperKey, QString()}).ok() &&
                 set({QStringLiteral("--add"), helperKey, helper}).ok();
        } else {
            ok = set({userKey, account}).ok();
        }
        if (!ok) {
            failed << QDir(target).dirName();
        }
    }

    if (!failed.isEmpty()) {
        emit historyEditFinished(
            verb, false, false,
            QObject::tr("Could not set the account in %1.").arg(failed.join(QStringLiteral(", "))));
        return;
    }
    const int submodules = static_cast<int>(targets.size()) - 1;
    const QString who = account.isEmpty() ? QObject::tr("git's default account") : account;
    emit historyEditFinished(
        verb, true, false,
        submodules > 0
            ? QObject::tr("%1 and %n submodule(s) now use %2 on %3.", nullptr, submodules)
                  .arg(QDir(workdir_).dirName(), who, host)
            : QObject::tr("%1 now uses %2 on %3.").arg(QDir(workdir_).dirName(), who, host));
}

} // namespace gity::session
