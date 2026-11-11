// RepoWorker: everything that talks to a remote — fetch, pull, push,
// clone, remote configuration and LFS locks.
#include "RepoWorker.h"

#include "core/git/Submodules.h"

#include "CredentialInventory.h"

#include <QDir>

namespace gity::session {

void RepoWorker::runForcePush(const QString& branch, const QString& remote,
                              const QString& remoteBranch, const QString& leaseOid) {
    // --force-with-lease with the expected value spelled out: git refuses if
    // the remote branch is anywhere but where it was last seen, so a push
    // made since by someone else is never silently overwritten.
    runNetwork(QObject::tr("Force push"),
               {QStringLiteral("push"), QStringLiteral("--progress"),
                QStringLiteral("--force-with-lease=refs/heads/%1:%2").arg(remoteBranch, leaseOid),
                remote, QStringLiteral("refs/heads/%1:refs/heads/%2").arg(branch, remoteBranch)});
}

namespace {

/// `-c remote.<name>.followRemoteHEAD=always` for each of `dir`'s remotes the
/// user has not configured that for themselves.
///
/// Each remote's default branch as it is now, not as it was at clone time: git
/// records it once, in <remote>/HEAD, and a project that moves its default
/// from release to release leaves that copy pointing at an old one. git 2.48
/// can follow it on fetch; older versions ignore the setting.
QStringList followRemoteHeadArgs(const QString& dir) {
    const GitProcess::InternalScope internal;
    const GitResult remotes = GitProcess::run(dir, {QStringLiteral("remote")});
    const GitResult configured =
        GitProcess::run(dir, {QStringLiteral("config"), QStringLiteral("--get-regexp"),
                              QStringLiteral("^remote\\..*\\.followremotehead$")});
    QStringList args;
    for (const QString& name : remotes.output.split(QChar('\n'), Qt::SkipEmptyParts)) {
        const QString key = QStringLiteral("remote.%1.followremotehead ").arg(name.trimmed());
        if (configured.output.contains(key, Qt::CaseInsensitive)) {
            continue;
        }
        args << QStringLiteral("-c")
             << QStringLiteral("remote.%1.followRemoteHEAD=always").arg(name.trimmed());
    }
    return args;
}

QStringList fetchArgs(const QString& dir) {
    // --recurse-submodules=no: submodules are fetched one by one below, with
    // --all, which git's own recursion does not pass down — it fetches only
    // each submodule's default remote, and stops being informative at the
    // first one that fails.
    return followRemoteHeadArgs(dir)
           << QStringLiteral("fetch") << QStringLiteral("--all") << QStringLiteral("--prune")
           << QStringLiteral("--recurse-submodules=no") << QStringLiteral("--progress");
}

} // namespace

void RepoWorker::runFetch(bool withSubmodules) {
    const QString verb = QStringLiteral("Fetch");
    emit networkStarted(verb);
    if (!repo_) {
        emit networkFinished(verb, false, QObject::tr("No repository is open."), {});
        return;
    }

    const GitResult result = GitProcess::runStreaming(
        workdir_, fetchArgs(workdir_), [this](const QString& line) { reportProgress(line); },
        600000);
    const gity::git::NetworkOutcome outcome = gity::git::classifyNetwork(
        result.ok(), result.output.toStdString(), result.errorOutput.toStdString());
    if (!result.ok()) {
        // The repository itself failed: its submodules are almost always on
        // the same host behind the same credentials, and would only repeat it.
        emit networkFinished(verb, false, describeFailure(verb, result, outcome),
                             result.errorOutput);
        return;
    }
    QString summary = outcome.summary.empty() ? QObject::tr("already up to date")
                                              : QString::fromStdString(outcome.summary);
    QString errorOutput = result.errorOutput;

    if (withSubmodules) {
        // Initialised submodules only, nested ones included — an uninitialised
        // one has no repository on disk to fetch into.
        QStringList paths;
        {
            const GitProcess::InternalScope internal;
            const GitResult listed =
                GitProcess::run(workdir_, {QStringLiteral("submodule"), QStringLiteral("foreach"),
                                           QStringLiteral("--recursive"), QStringLiteral("--quiet"),
                                           QStringLiteral("echo \"$displaypath\"")});
            paths = listed.output.split(QChar('\n'), Qt::SkipEmptyParts);
        }
        QStringList failed;
        int fetched = 0;
        for (const QString& path : std::as_const(paths)) {
            const QString dir = QDir(workdir_).filePath(path.trimmed());
            const GitResult sub = GitProcess::runStreaming(
                dir, fetchArgs(dir),
                [this, path](const QString& line) {
                    gity::git::NetworkProgress progress;
                    if (gity::git::parseProgress(line.toStdString(), &progress)) {
                        emit networkProgress(QStringLiteral("%1: %2").arg(
                                                 path, QString::fromStdString(progress.phase)),
                                             progress.percent);
                    }
                },
                600000);
            errorOutput += sub.errorOutput;
            if (sub.ok()) {
                ++fetched;
                continue;
            }
            const gity::git::NetworkOutcome subOutcome = gity::git::classifyNetwork(
                false, sub.output.toStdString(), sub.errorOutput.toStdString());
            failed << QStringLiteral("%1: %2").arg(path, describeFailure(verb, sub, subOutcome));
        }
        if (!failed.isEmpty()) {
            emit networkFinished(
                verb, false,
                QObject::tr("The repository was fetched (%1), but %n submodule(s) could not be:",
                            nullptr, static_cast<int>(failed.size()))
                        .arg(summary) +
                    QStringLiteral("\n") + failed.join(QChar('\n')),
                errorOutput);
            refreshStatus();
            return;
        }
        if (fetched > 0) {
            summary += QObject::tr(" · %n submodule(s) fetched", nullptr, fetched);
        }
    }
    emit networkFinished(verb, true, summary, errorOutput);
    refreshStatus();
}

void RepoWorker::runNetwork(const QString& verb, const QStringList& args) {
    // Never return without a matching networkFinished: the UI disables the
    // network actions on start and re-enables them on finish, so a silent
    // early return would leave them disabled for the rest of the session.
    emit networkStarted(verb);
    if (!repo_) {
        emit networkFinished(verb, false, QObject::tr("No repository is open."), {});
        return;
    }

    // Network operations are slow and the timeout has to allow for a large
    // fetch on a poor connection; ten minutes is generous rather than tight.
    // A pull moves the branch and rewrites files, so it can be undone; a
    // fetch or a push changes nothing here that undo could put back.
    const bool undoable = args.contains(QStringLiteral("pull"));
    RepoState undoBefore = undoable ? beginUndo() : RepoState{};
    const GitResult result = GitProcess::runStreaming(
        workdir_, args, [this](const QString& line) { reportProgress(line); }, 600000);
    if (undoable) {
        endUndo(verb, std::move(undoBefore), result.ok());
    }

    // The parsing lives in core and is tested against real git output; this
    // function only chooses the words. See core/git/NetworkOutcome.h.
    const gity::git::NetworkOutcome outcome = gity::git::classifyNetwork(
        result.ok(), result.output.toStdString(), result.errorOutput.toStdString());

    if (result.ok()) {
        const QString summary = outcome.summary.empty() ? QObject::tr("already up to date")
                                                        : QString::fromStdString(outcome.summary);
        emit networkFinished(verb, true, summary, result.errorOutput);
        refreshStatus();
        return;
    }

    emit networkFinished(verb, false, describeFailure(verb, result, outcome), result.errorOutput);
}

QString RepoWorker::describeFailure(const QString& verb, const GitResult& result,
                                    const gity::git::NetworkOutcome& outcome) const {
    QString problem;
    switch (outcome.failure) {
    case gity::git::NetworkFailure::AuthRequired:
        // GIT_TERMINAL_PROMPT=0 turns a prompt no GUI could show into a clean
        // failure. Saying what it means beats passing git's wording through.
        // Reached when no credential was given: the prompt was cancelled, or
        // the remote refused what the credential helper supplied.
        problem = QObject::tr("%1 was refused for lack of valid credentials. Sign in under "
                              "Tools ▸ Accounts, check what your credential helper supplies "
                              "for this host, or use an SSH key.")
                      .arg(verb);
        // The commonest cause on GitHub, named exactly when it applies: the
        // address names a user GitHub CLI will not answer for.
        for (const auto& remote : gity::git::listRemotes(repo_.get())) {
            const QString hint =
                CredentialInventory::accountHint(QString::fromStdString(remote.fetchUrl));
            if (!hint.isEmpty()) {
                problem = hint;
                break;
            }
        }
        break;
    case gity::git::NetworkFailure::Diverged:
        // Pull is --ff-only on purpose (see requestPull), so a diverged branch
        // stops here. That is a decision only the user can make.
        // The message used to say these verbs did not exist yet. They do —
        // stale guidance that sends someone to a terminal for something the
        // application can do is worse than none.
        problem = QObject::tr(
            "Your branch and its upstream have both moved on, so this pull cannot "
            "fast-forward. Fetch has already brought the remote work down, so nothing is "
            "lost — reconcile them with Merge or Rebase, whichever your team prefers.");
        break;
    case gity::git::NetworkFailure::RejectedNonFastForward:
        problem = QObject::tr(
            "The remote has commits your branch does not, so this push was rejected. Fetch "
            "first, then reconcile the two histories — pushing over them would discard "
            "someone else's work. If you rewrote this branch yourself, press Push again: its "
            "preview offers a force push with a lease.");
        break;
    case gity::git::NetworkFailure::NotFoundOrNoAccess:
        // The wording matters here. GitHub answers 404 rather than 403 for a
        // private repository the authenticated account cannot see, so git's
        // "repository not found" sends people hunting for a typo in a URL that
        // is correct. The usual cause is the wrong account — a work repository
        // reached with personal credentials, or an organisation that requires
        // SSO authorisation for the token.
        problem = QObject::tr(
            "The remote says this repository does not exist. If the URL is right, the "
            "credentials being used most likely cannot see it — a private repository answers "
            "the same way whether it is missing or merely out of reach. Check which account "
            "your credential helper is supplying, and whether the repository belongs to an "
            "organisation that needs a different one.");
        break;
    case gity::git::NetworkFailure::LeaseRejected:
        problem = QObject::tr(
            "Stopped: someone pushed to this branch after you last fetched, and forcing now "
            "would have removed their work. Fetch, look at what arrived, and decide again.");
        break;
    case gity::git::NetworkFailure::RejectedOther:
        // A hook or a protected branch. Git's own line is the only thing that
        // carries the reason, so it goes through verbatim.
        problem = QObject::tr("The remote refused this push: %1")
                      .arg(QString::fromStdString(outcome.detail));
        break;
    case gity::git::NetworkFailure::NoRepository:
    case gity::git::NetworkFailure::Unknown:
    case gity::git::NetworkFailure::None:
        problem = QString::fromStdString(outcome.detail);
        break;
    }
    if (problem.isEmpty()) {
        problem = result.firstProblemLine();
    }
    return problem;
}

void RepoWorker::reportProgress(const QString& line) {
    gity::git::NetworkProgress progress;
    if (gity::git::parseProgress(line.toStdString(), &progress)) {
        emit networkProgress(QString::fromStdString(progress.phase), progress.percent);
    }
}

void RepoWorker::runClone(const QString& url, const QString& parentDir, const QString& folder) {
    const QString verb = QObject::tr("Clone");
    emit networkStarted(verb);

    // An hour: cloning a large repository with years of binary history over a
    // domestic connection is genuinely a long operation, and timing it out
    // half way leaves a partial directory behind for the user to clean up.
    const GitResult result = GitProcess::runInParent(
        parentDir,
        // `--` before the URL: a pasted "URL" beginning with a dash would
        // otherwise be read as an option, and clone has options that run
        // commands.
        {QStringLiteral("clone"), QStringLiteral("--progress"), QStringLiteral("--"), url, folder},
        [this](const QString& line) { reportProgress(line); }, 3600000);

    const gity::git::NetworkOutcome outcome = gity::git::classifyNetwork(
        result.ok(), result.output.toStdString(), result.errorOutput.toStdString());

    if (result.ok()) {
        const QString path = QDir(parentDir).filePath(folder);
        emit networkFinished(verb, true, QObject::tr("cloned into %1").arg(folder),
                             result.errorOutput);
        emit cloneFinished(path);
        return;
    }

    QString problem = QString::fromStdString(outcome.detail);
    if (outcome.failure == gity::git::NetworkFailure::AuthRequired) {
        problem = QObject::tr("This repository was refused for lack of valid credentials. Sign "
                              "in under Tools ▸ Accounts, check what your credential helper "
                              "supplies for this host, or use an SSH key.");
    }
    if (problem.isEmpty()) {
        problem = result.firstProblemLine();
    }
    emit networkFinished(verb, false, problem, result.errorOutput);
}

void RepoWorker::loadRemotes() {
    if (!repo_) {
        return;
    }
    emit remotesReady(std::make_shared<const std::vector<gity::git::RemoteEntry>>(
        gity::git::listRemotes(repo_.get())));
}

void RepoWorker::runRemoteCommand(const QStringList& args) {
    if (!repo_) {
        return;
    }
    const GitResult result = GitProcess::run(workdir_, args);
    if (!result.ok()) {
        // git's own words: it knows exactly why it refused, and every reason
        // it gives here is one the user can act on.
        emit remoteCommandFailed(result.firstProblemLine());
    }
    // Either way the list is re-read, so a partial failure cannot leave the
    // dialog showing something the repository does not agree with. The refs
    // sidebar is refreshed by the window reopening the repository once the
    // dialog closes — removing a remote takes its tracking refs with it.
    loadRemotes();
}

QString RepoWorker::publishRemote(QString* problem) const {
    // origin unless the repository has exactly one other remote; guessing
    // beyond that would publish somewhere the user did not choose.
    const std::vector<gity::git::RemoteEntry> remotes = gity::git::listRemotes(repo_.get());
    const bool hasOrigin =
        std::any_of(remotes.begin(), remotes.end(),
                    [](const gity::git::RemoteEntry& entry) { return entry.name == "origin"; });
    if (hasOrigin) {
        return QStringLiteral("origin");
    }
    if (remotes.size() == 1) {
        return QString::fromStdString(remotes.front().name);
    }
    if (problem != nullptr) {
        *problem = remotes.empty()
                       ? QObject::tr("This repository has no remote to publish to. Add one "
                                     "under Repository ▸ Remotes.")
                       : QObject::tr("This repository has several remotes and none is called "
                                     "origin, so there is no obvious one to publish to.");
    }
    return {};
}

void RepoWorker::runPushBranch(const QString& branch) {
    const QString verb = QObject::tr("Push");
    if (!repo_) {
        emit networkStarted(verb);
        emit networkFinished(verb, false, QObject::tr("No repository is open."), {});
        return;
    }

    // A branch that already tracks something is pushed *there* — to its own
    // remote and its own upstream name. `push --set-upstream origin <name>`
    // for every branch re-pointed one tracking `upstream/<name>` at origin,
    // and pushed one tracking a differently named branch to a new one.
    const QByteArray refname = QStringLiteral("refs/heads/%1").arg(branch).toUtf8();
    git_buf remoteBuf = GIT_BUF_INIT;
    git_buf mergeBuf = GIT_BUF_INIT;
    const bool tracked =
        git_branch_upstream_remote(&remoteBuf, repo_.get(), refname.constData()) == 0 &&
        git_branch_upstream_merge(&mergeBuf, repo_.get(), refname.constData()) == 0;
    const QString trackedRemote =
        tracked ? QString::fromUtf8(remoteBuf.ptr, static_cast<qsizetype>(remoteBuf.size))
                : QString();
    const QString trackedMerge =
        tracked ? QString::fromUtf8(mergeBuf.ptr, static_cast<qsizetype>(mergeBuf.size))
                : QString();
    git_buf_dispose(&remoteBuf);
    git_buf_dispose(&mergeBuf);

    // A triangular setup — fetch from upstream, push to a fork — names where
    // pushes go separately, and that wins, exactly as it does for `git push`.
    QString pushRemote;
    {
        git_config* rawConfig = nullptr;
        if (git_repository_config_snapshot(&rawConfig, repo_.get()) == 0) {
            const gity::git::Handle<git_config, git_config_free> config(rawConfig);
            const QByteArray keys[] = {QStringLiteral("branch.%1.pushRemote").arg(branch).toUtf8(),
                                       QByteArrayLiteral("remote.pushDefault")};
            for (const QByteArray& key : keys) {
                const char* value = nullptr;
                if (git_config_get_string(&value, config.get(), key.constData()) == 0 &&
                    value != nullptr && *value != '\0') {
                    pushRemote = QString::fromUtf8(value);
                    break;
                }
            }
        }
    }

    // A branch tracking another local branch (remote ".") has nowhere to be
    // pushed to; publishing it is the useful reading.
    if (tracked && trackedRemote != QStringLiteral(".")) {
        if (!pushRemote.isEmpty() && pushRemote != trackedRemote) {
            runNetwork(verb,
                       {QStringLiteral("push"), QStringLiteral("--progress"), pushRemote, branch});
        } else {
            runNetwork(verb,
                       {QStringLiteral("push"), QStringLiteral("--progress"), trackedRemote,
                        QStringLiteral("%1:%2").arg(QString::fromUtf8(refname), trackedMerge)});
        }
        return;
    }

    QString problem;
    const QString remote = !pushRemote.isEmpty() ? pushRemote : publishRemote(&problem);
    if (remote.isEmpty()) {
        emit networkStarted(verb);
        emit networkFinished(verb, false, problem, {});
        return;
    }
    runNetwork(verb, {QStringLiteral("push"), QStringLiteral("--progress"),
                      QStringLiteral("--set-upstream"), remote, branch});
}

void RepoWorker::runPush() {
    const QString verb = QObject::tr("Push");
    if (!repo_) {
        emit networkStarted(verb);
        emit networkFinished(verb, false, QObject::tr("No repository is open."), {});
        return;
    }

    // A branch with no upstream cannot be pushed by `git push` alone — git
    // refuses and prints the --set-upstream command it wants. Publishing a new
    // branch is the ordinary meaning of pressing Push on one, so that case is
    // handed to runPushBranch, which supplies the flag.
    //
    // Everything else is a plain `git push`, so push.default, pushRemote and
    // the rest of the user's configuration decide, as in their terminal.
    // Detached or unborn HEAD lands here too: git's own refusal says why
    // better than a guess would.
    git_reference* rawHead = nullptr;
    if (git_repository_head(&rawHead, repo_.get()) == 0) {
        const gity::git::Handle<git_reference, git_reference_free> head(rawHead);
        if (git_reference_is_branch(head.get()) != 0) {
            git_reference* rawUpstream = nullptr;
            if (git_branch_upstream(&rawUpstream, head.get()) == 0) {
                git_reference_free(rawUpstream);
            } else if (const char* shorthand = git_reference_shorthand(head.get());
                       shorthand != nullptr) {
                runPushBranch(QString::fromUtf8(shorthand));
                return;
            }
        }
    }
    runNetwork(verb, {QStringLiteral("push"), QStringLiteral("--progress")});
}

void RepoWorker::runPushTag(const QString& name) {
    const QString verb = QObject::tr("Push tag");
    if (!repo_) {
        emit networkStarted(verb);
        emit networkFinished(verb, false, QObject::tr("No repository is open."), {});
        return;
    }
    QString problem;
    const QString remote = publishRemote(&problem);
    if (remote.isEmpty()) {
        emit networkStarted(verb);
        emit networkFinished(verb, false, problem, {});
        return;
    }
    // The refspec is explicit rather than --tags: pushing every tag at once
    // publishes ones the user may never have meant to share.
    runNetwork(verb, {QStringLiteral("push"), QStringLiteral("--progress"), remote,
                      QStringLiteral("refs/tags/%1").arg(name)});
}

void RepoWorker::loadLocks() {
    if (!repo_) {
        return;
    }
    // A network call, so it gets its own generous timeout but is never on a
    // path that blocks showing the working copy.
    const GitResult result = GitProcess::run(
        workdir_, {QStringLiteral("lfs"), QStringLiteral("locks"), QStringLiteral("--json")},
        60000);

    if (!result.ok()) {
        // A remote without the locking API, or no LFS at all. Both are
        // ordinary: most repositories do not lock, and the screen must not
        // report a problem for it.
        emit locksReady(std::make_shared<const std::vector<gity::git::LfsLock>>(), false);
        return;
    }
    emit locksReady(std::make_shared<const std::vector<gity::git::LfsLock>>(
                        gity::git::parseLfsLocks(result.output.toStdString())),
                    true);
}

void RepoWorker::runLockCommand(const QString& verb, const QStringList& args) {
    if (!repo_) {
        return;
    }
    const GitResult result = GitProcess::run(workdir_, args, 60000);
    if (!result.ok()) {
        emit historyEditFinished(verb, false, false, result.firstProblemLine());
    } else {
        const std::vector<std::string> lines =
            gity::git::splitGitOutput(result.output.toStdString());
        emit historyEditFinished(verb, true, false,
                                 lines.empty() ? QString() : QString::fromStdString(lines.front()));
    }
    loadLocks();
}

} // namespace gity::session
