#include "RepoSession.h"

#include "core/git/MergePrediction.h"
#include "core/git/NetworkOutcome.h"
#include "core/git/Provider.h"
#include "core/git/RepoAccount.h"
#include "core/git/Submodules.h"
#include "core/git/OperationState.h"

#include "core/git/HistoryStream.h"
#include "core/git/Repository.h"
#include "core/git/WorkingCopy.h"
#include "GitProcess.h"
#include "CredentialInventory.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

namespace gity::session {
namespace {

QString headRefName(git_repository* repo) {
    git_reference* raw = nullptr;
    if (git_repository_head(&raw, repo) < 0) {
        return QObject::tr("no commits yet");
    }
    const gity::git::Handle<git_reference, git_reference_free> head(raw);
    const char* shorthand = git_reference_shorthand(head.get());
    const QString name =
        shorthand != nullptr ? QString::fromUtf8(shorthand) : QStringLiteral("HEAD");

    // Said, not implied. A detached HEAD's shorthand is the short id, which
    // reads exactly like a branch name — and a commit made here is lost by the
    // next checkout, which is not a surprise anyone should get afterwards.
    if (git_repository_head_detached(repo) == 1) {
        return QObject::tr("detached at %1").arg(name);
    }
    return name;
}

} // namespace

namespace {

QString readTrimmed(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

} // namespace

RepoWorker::RepoWorker(std::atomic<bool>& cancelFlag) : cancel_(cancelFlag) {}

void RepoWorker::setFilter(const gity::git::CommitFilter& filter) {
    filter_ = filter;
}

void RepoWorker::rewalk(const gity::git::CommitFilter& filter) {
    if (workdir_.isEmpty()) {
        return;
    }
    filter_ = filter;
    // Re-opening is the whole walk again, which is the honest cost: a filtered
    // view is a different set of commits with different lanes, not the same
    // graph with rows hidden.
    open(workdir_);
}

void RepoWorker::open(const QString& path) {
    try {
        // One git_repository per worker thread. Nothing derived from it leaves
        // this function.
        repo_ = gity::git::openRepository(path.toStdString());
        git_repository* repo = repo_.get();

        const char* workdirRaw = git_repository_workdir(repo);
        RepoInfo info;
        // git_repository_workdir returns a trailing slash and the caller's
        // path does not. Everything downstream compares these as strings — the
        // repository tabs did, and every reopen after a branch delete, merge or
        // fetch looked like a different repository and wiped the tab set. One
        // spelling leaves here.
        info.workdir = QDir(workdirRaw != nullptr ? QString::fromUtf8(workdirRaw) : path)
                           .absolutePath();
        workdir_ = info.workdir;
        info.headRef = headRefName(repo);
        emit opened(info);
        undo_.load(workdir_, QString::fromUtf8(git_repository_path(repo)));
        reportUndo();

        // Refs before history: the sidebar is the first thing that can be
        // filled, and it is cheap next to the walk.
        emit refsReady(std::make_shared<const gity::git::RefSet>(gity::git::loadRefs(repo)));
        refreshStatus();

        gity::git::StreamOptions options;
        options.filter = filter_;
        options.cancel = &cancel_;
        const auto stats = gity::git::streamHistory(
            repo, options, [this](model::HistoryChunk&& chunk) {
                if (cancel_.load(std::memory_order_relaxed)) {
                    return false;
                }
                emit chunkReady(std::make_shared<const model::HistoryChunk>(std::move(chunk)));
                return true;
            });

        emit finished(static_cast<quint64>(stats.commits), stats.firstChunkMs, stats.totalMs,
                      stats.cancelled);
    } catch (const gity::git::GitException& e) {
        emit failed(QString::fromStdString(e.error().message),
                    QString::fromStdString(e.error().raw));
    } catch (const std::exception& e) {
        emit failed(QString::fromUtf8(e.what()), {});
    }
}

void RepoWorker::loadDetail(const QString& oidHex, quint64 generation) {
    if (!repo_) {
        return;
    }
    git_oid oid{};
    if (git_oid_fromstr(&oid, oidHex.toLatin1().constData()) < 0) {
        return;
    }
    try {
        emit detailReady(std::make_shared<const gity::git::CommitDetail>(
                             gity::git::loadCommitDetail(repo_.get(), oid)),
                         generation);
    } catch (const gity::git::GitException&) {
        // A detail load failing is not worth a dialog: the row simply shows
        // nothing, and the next selection tries again.
    }
}

void RepoWorker::loadCommitPeek(const QString& oidHex, const QString& path) {
    if (!repo_) {
        return;
    }
    git_oid oid{};
    if (git_oid_fromstr(&oid, oidHex.toLatin1().constData()) < 0) {
        return;
    }
    try {
        if (path.isEmpty()) {
            emit commitPeekReady(oidHex, std::make_shared<const gity::git::CommitDetail>(
                                             gity::git::loadCommitDetail(repo_.get(), oid)));
        } else {
            emit commitPeekDiffReady(oidHex, path,
                                     std::make_shared<const gity::git::FileDiff>(
                                         gity::git::loadFileDiff(repo_.get(), oid,
                                                                 path.toStdString())));
        }
    } catch (const gity::git::GitException&) {
        // As loadDetail: the pane shows nothing, and the next pick tries again.
    }
}

void RepoWorker::loadFileDiff(const QString& oidHex, const QString& path, quint64 generation) {
    if (!repo_) {
        return;
    }
    git_oid oid{};
    if (git_oid_fromstr(&oid, oidHex.toLatin1().constData()) < 0) {
        return;
    }
    try {
        emit fileDiffReady(std::make_shared<const gity::git::FileDiff>(gity::git::loadFileDiff(
                               repo_.get(), oid, path.toStdString())),
                           generation);
    } catch (const gity::git::GitException&) {
        // Same reasoning as loadDetail: the pane shows nothing and the next
        // selection tries again, which beats a dialog per failed diff.
    }
}

void RepoWorker::loadComparison(const QString& fromHex, const QString& toHex,
                                quint64 generation) {
    if (!repo_) {
        return;
    }
    git_oid from{};
    git_oid to{};
    if (git_oid_fromstr(&from, fromHex.toLatin1().constData()) < 0 ||
        git_oid_fromstr(&to, toHex.toLatin1().constData()) < 0) {
        return;
    }
    try {
        emit comparisonReady(std::make_shared<const gity::git::Comparison>(
                                 gity::git::loadComparison(repo_.get(), from, to)),
                             generation);
    } catch (const gity::git::GitException& e) {
        qWarning("gity: could not compare: %s", e.error().message.c_str());
    }
}

void RepoWorker::loadFileDiffBetween(const QString& fromHex, const QString& toHex,
                                     const QString& path, quint64 generation) {
    if (!repo_) {
        return;
    }
    git_oid from{};
    git_oid to{};
    if (git_oid_fromstr(&from, fromHex.toLatin1().constData()) < 0 ||
        git_oid_fromstr(&to, toHex.toLatin1().constData()) < 0) {
        return;
    }
    try {
        emit fileDiffReady(std::make_shared<const gity::git::FileDiff>(
                               gity::git::loadFileDiffBetween(repo_.get(), from, to,
                                                              path.toStdString())),
                           generation);
    } catch (const gity::git::GitException&) {
        // As loadFileDiff: the pane shows nothing, the next selection retries.
    }
}

bool RepoWorker::operationInProgress() const {
    const char* path = repo_ ? git_repository_path(repo_.get()) : nullptr;
    if (path == nullptr) {
        return false;
    }
    const gity::git::Operation operation = gity::git::detectOperation(path);
    return operation != gity::git::Operation::None && operation != gity::git::Operation::Bisect;
}

bool RepoWorker::worktreeClean() const {
    if (!repo_) {
        return true;
    }
    try {
        return gity::git::loadStatus(repo_.get()).entries.empty();
    } catch (const gity::git::GitException&) {
        return false; // unknown: take the full snapshot rather than risk a thin one
    }
}

RepoState RepoWorker::beginUndo() {
    if (!repo_) {
        return {};
    }
    return undo_.capture(true, worktreeClean());
}

void RepoWorker::endUndo(const QString& verb, RepoState before, bool filesChanged) {
    if (!repo_ || (before.headOid.isEmpty() && before.refs.empty())) {
        return;
    }
    // Abandoning something Gity did not start has nothing of ours to undo;
    // abandoning something it did closes that entry (UndoLog::record).
    if (verb == QStringLiteral("Abort") && !undo_.hasPending()) {
        return;
    }
    undo_.record(verb, std::move(before), filesChanged, operationInProgress());
    reportUndo();
}

void RepoWorker::reportUndo() {
    emit undoStateChanged(undo_.undoLabel(), undo_.redoLabel());
}

void RepoWorker::undo() {
    const UndoLog::Outcome outcome = undo_.undo(operationInProgress(), worktreeClean());
    emit historyEditFinished(QObject::tr("Undo"), outcome.ok, false, outcome.message);
    reportUndo();
    reportOperation();
    refreshStatus();
}

void RepoWorker::redo() {
    const UndoLog::Outcome outcome = undo_.redo(operationInProgress(), worktreeClean());
    emit historyEditFinished(QObject::tr("Redo"), outcome.ok, false, outcome.message);
    reportUndo();
    reportOperation();
    refreshStatus();
}

namespace {

/// "abc12345  subject" for up to ten commits.
QStringList describeCommits(const std::vector<gity::git::ComparedCommit>& commits) {
    QStringList lines;
    for (std::size_t i = 0; i < commits.size() && i < 10; ++i) {
        char buffer[GIT_OID_SHA1_HEXSIZE + 1] = {};
        git_oid_tostr(buffer, sizeof(buffer), &commits[i].oid);
        lines << QStringLiteral("%1  %2").arg(QString::fromLatin1(buffer).left(8),
                                               QString::fromStdString(commits[i].summary));
    }
    return lines;
}

QString trimmedOutput(const GitResult& result) {
    return result.output.section(QChar('\n'), 0, 0).trimmed();
}

} // namespace

void RepoWorker::computePreview(int kindValue, const QString& target, const QString& branchName) {
    auto preview = std::make_shared<Preview>();
    preview->kind = static_cast<Preview::Kind>(kindValue);
    preview->target = target;
    const auto finish = [this, &preview] { emit previewReady(preview); };
    if (!repo_) {
        preview->problem = QObject::tr("No repository is open.");
        finish();
        return;
    }
    if (operationInProgress()) {
        preview->problem = QObject::tr("Finish or abandon the operation in progress first.");
        finish();
        return;
    }

    const GitProcess::InternalScope internal;
    const auto resolve = [this](const QString& rev) {
        const GitResult result = GitProcess::run(
            workdir_, {QStringLiteral("rev-parse"), QStringLiteral("-q"), QStringLiteral("--verify"),
                       rev + QStringLiteral("^{commit}")});
        return result.ok() ? trimmedOutput(result) : QString();
    };
    const auto config = [this](const QString& key) {
        return trimmedOutput(GitProcess::run(
            workdir_, {QStringLiteral("config"), QStringLiteral("--get"), key}));
    };

    // The branch acted on: the one named, or the one checked out.
    QString branch = branchName;
    if (branch.isEmpty()) {
        const GitResult head = GitProcess::run(
            workdir_, {QStringLiteral("symbolic-ref"), QStringLiteral("-q"), QStringLiteral("--short"),
                       QStringLiteral("HEAD")});
        branch = head.ok() ? trimmedOutput(head) : QString();
    }
    if (branch.isEmpty()) {
        preview->problem = QObject::tr("HEAD is detached — on no branch — so there is nothing "
                                       "to pull into or push from. Make a branch first.");
        finish();
        return;
    }
    preview->branch = branch;

    const bool isPull = preview->kind == Preview::Kind::Pull;
    const bool isPush = preview->kind == Preview::Kind::Push;
    QString other = target;
    if (isPull || isPush) {
        const GitResult upstream = GitProcess::run(
            workdir_, {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"),
                       QStringLiteral("--symbolic-full-name"), branch + QStringLiteral("@{upstream}")});
        preview->remote = config(QStringLiteral("branch.%1.remote").arg(branch));
        preview->remoteBranch =
            config(QStringLiteral("branch.%1.merge").arg(branch)).remove(QStringLiteral("refs/heads/"));
        if (!upstream.ok() || preview->remote.isEmpty() || preview->remote == QStringLiteral(".")) {
            if (isPull) {
                preview->problem = QObject::tr("%1 tracks no remote branch, so there is nothing "
                                               "to pull. Push it first to publish it.")
                                       .arg(branch);
                finish();
                return;
            }
            // Publishing: everything not already on some remote goes.
            preview->newBranch = true;
            QString problem;
            preview->remote = publishRemote(&problem);
            preview->remoteBranch = branch;
            if (preview->remote.isEmpty()) {
                preview->problem = problem;
                finish();
                return;
            }
            preview->target = QStringLiteral("%1/%2").arg(preview->remote, branch);
            const GitResult count = GitProcess::run(
                workdir_, {QStringLiteral("rev-list"), QStringLiteral("--count"), branch,
                           QStringLiteral("--not"), QStringLiteral("--remotes")});
            preview->outgoing = trimmedOutput(count).toInt();
            const GitResult log = GitProcess::run(
                workdir_, {QStringLiteral("log"), QStringLiteral("-10"),
                           QStringLiteral("--format=%h%x00%s"), branch, QStringLiteral("--not"),
                           QStringLiteral("--remotes")});
            for (const QString& line : log.output.split(QChar('\n'), Qt::SkipEmptyParts)) {
                preview->outgoingCommits << line.section(QChar('\0'), 0, 0).leftJustified(8) +
                                                QStringLiteral("  ") +
                                                line.section(QChar('\0'), 1);
            }
            finish();
            return;
        }
        other = trimmedOutput(upstream);
        preview->target = other;
    }

    if (isPull) {
        // Of the remote as it is now, not as last fetched: that is what the
        // pull will actually bring in. Progress shows in the status bar; this
        // is not a Fetch the user asked for, so it does not announce one.
        const GitProcess::VisibleScope visible;
        const GitResult fetched = GitProcess::runStreaming(
            workdir_, {QStringLiteral("fetch"), QStringLiteral("--progress"), preview->remote},
            [this](const QString& line) { reportProgress(line); }, 600000);
        if (!fetched.ok()) {
            preview->problem = QObject::tr("Could not check %1: %2")
                                   .arg(preview->remote, fetched.firstProblemLine());
            finish();
            return;
        }
    }

    const QString local = resolve(QStringLiteral("refs/heads/%1").arg(branch));
    const QString theirs = resolve(other);
    if (local.isEmpty() || theirs.isEmpty()) {
        preview->problem = QObject::tr("Could not find %1.").arg(theirs.isEmpty() ? other : branch);
        finish();
        return;
    }

    git_oid localOid{};
    git_oid theirOid{};
    git_oid_fromstr(&localOid, local.toLatin1().constData());
    git_oid_fromstr(&theirOid, theirs.toLatin1().constData());
    gity::git::Comparison comparison;
    try {
        comparison = gity::git::loadComparison(repo_.get(), localOid, theirOid);
    } catch (const gity::git::GitException& e) {
        preview->problem = QString::fromStdString(e.error().message);
        finish();
        return;
    }
    preview->incoming = static_cast<int>(comparison.onlyInTo);
    preview->outgoing = static_cast<int>(comparison.onlyInFrom);
    preview->incomingCommits = describeCommits(comparison.onlyInToCommits);
    preview->outgoingCommits = describeCommits(comparison.onlyInFromCommits);

    if (isPush) {
        preview->upToDate = preview->outgoing == 0 && preview->incoming == 0;
        // The remote has commits this branch does not: a plain push will be
        // rejected, and a force push would remove them. Leased against what
        // was last fetched, so anything pushed since makes it fail instead.
        if (preview->incoming > 0) {
            preview->leaseOid = theirs;
        }
        static const QStringList sharedNames{QStringLiteral("main"), QStringLiteral("master"),
                                             QStringLiteral("develop"), QStringLiteral("trunk")};
        const QString remoteHead = trimmedOutput(GitProcess::run(
            workdir_, {QStringLiteral("symbolic-ref"), QStringLiteral("-q"), QStringLiteral("--short"),
                       QStringLiteral("refs/remotes/%1/HEAD").arg(preview->remote)}));
        preview->shared = sharedNames.contains(preview->remoteBranch) ||
                          preview->remoteBranch.startsWith(QStringLiteral("release/")) ||
                          remoteHead == QStringLiteral("%1/%2").arg(preview->remote,
                                                                     preview->remoteBranch);
        finish();
        return;
    }

    const bool rebase = preview->kind == Preview::Kind::Rebase;
    preview->upToDate = preview->incoming == 0;
    preview->fastForward = !rebase && preview->outgoing == 0 && preview->incoming > 0;

    // Files that would stop git before it starts. A merge refuses to
    // overwrite uncommitted changes to files it touches; a rebase needs a
    // clean working copy altogether.
    try {
        const gity::git::WorkingCopyStatus status = gity::git::loadStatus(repo_.get());
        for (const auto& entry : status.entries) {
            if (entry.isUntracked()) {
                continue;
            }
            const bool touched = std::any_of(
                comparison.files.begin(), comparison.files.end(),
                [&entry](const gity::git::ChangedFile& file) { return file.path == entry.path; });
            if (rebase || touched) {
                preview->blockedByLocal << QString::fromStdString(entry.path);
            }
        }
    } catch (const gity::git::GitException&) {
    }

    // A trial merge in memory, when there is anything to merge.
    const bool diverged = preview->incoming > 0 && preview->outgoing > 0;
    if (diverged) {
        const QString version = GitProcess::version();
        const bool supported = gity::git::mergeTreeSupported(
            version.section(QChar('.'), 0, 0).toInt(), version.section(QChar('.'), 1, 1).toInt());
        if (!supported) {
            preview->predictionNote =
                QObject::tr("Conflicts cannot be predicted with git %1; 2.38 or newer can.")
                    .arg(version);
        } else {
            const GitResult trial = GitProcess::run(
                workdir_, {QStringLiteral("merge-tree"), QStringLiteral("--write-tree"),
                           QStringLiteral("--name-only"), QStringLiteral("--no-messages"),
                           QStringLiteral("-z"), local, theirs});
            const gity::git::MergePrediction prediction = gity::git::parseMergeTree(
                trial.started ? trial.exitCode : -1, trial.output.toStdString());
            preview->predicted = prediction.available;
            for (const std::string& path : prediction.conflicts) {
                preview->conflicts << QString::fromStdString(path);
            }
            if (!prediction.available) {
                preview->predictionNote = QObject::tr("Conflicts could not be predicted.");
            } else if (rebase) {
                preview->predictionNote = QObject::tr(
                    "Predicted by merging the two tips. A rebase replays commits one at a "
                    "time, so it can stop in the same files at a different step.");
            }
        }
    }
    finish();
}

void RepoWorker::runForcePush(const QString& branch, const QString& remote,
                              const QString& remoteBranch, const QString& leaseOid) {
    // --force-with-lease with the expected value spelled out: git refuses if
    // the remote branch is anywhere but where it was last seen, so a push
    // made since by someone else is never silently overwritten.
    runNetwork(QObject::tr("Force push"),
               {QStringLiteral("push"), QStringLiteral("--progress"),
                QStringLiteral("--force-with-lease=refs/heads/%1:%2").arg(remoteBranch, leaseOid),
                remote,
                QStringLiteral("refs/heads/%1:refs/heads/%2").arg(branch, remoteBranch)});
}

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
    const QString helper = configValue(
        workdir_, {QStringLiteral("config"), QStringLiteral("--get-urlmatch"),
                   QStringLiteral("credential.helper"), url});
    const bool viaGh = helper.contains(QStringLiteral("gh auth"));

    QStringList accounts;
    QString defaultAccount;
    for (const StoredCredential& credential : CredentialInventory::gather()) {
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

    // What this repository has chosen, in its own config.
    QString chosen;
    if (viaGh) {
        const GitResult helpers = GitProcess::run(
            workdir_, {QStringLiteral("config"), QStringLiteral("--local"), QStringLiteral("--get-all"),
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
    emit accountChoiceReady(host, viaGh, accounts, defaultAccount, chosen);
}

void RepoWorker::setAccountChoice(const QString& account) {
    const QString verb = QObject::tr("Account");
    if (!repo_) {
        return;
    }
    const QString host = httpsHostOf(gity::git::listRemotes(repo_.get()));
    if (host.isEmpty() || (!account.isEmpty() && !gity::git::isSafeAccountName(account.toStdString()))) {
        emit historyEditFinished(verb, false, false,
                                 QObject::tr("%1 is not an account name git can be told to use.")
                                     .arg(account));
        return;
    }
    const QString url = QStringLiteral("https://%1").arg(host);
    const bool viaGh = configValue(workdir_, {QStringLiteral("config"),
                                              QStringLiteral("--get-urlmatch"),
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
        const QString subUrl = configValue(path, {QStringLiteral("remote"), QStringLiteral("get-url"),
                                                  QStringLiteral("origin")});
        if (QString::fromStdString(gity::git::hostOfRemote(subUrl.toStdString()))
                    .compare(host, Qt::CaseInsensitive) == 0 &&
            subUrl.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
            targets << path;
        }
    }

    const QString helperKey = QStringLiteral("credential.%1.helper").arg(url);
    const QString userKey = QStringLiteral("credential.%1.username").arg(url);
    const QString helper =
        QString::fromStdString(gity::git::ghAccountHelper(host.toStdString(), account.toStdString()));
    QStringList failed;
    for (const QString& target : targets) {
        const auto set = [&target](const QStringList& args) {
            return GitProcess::run(target, QStringList{QStringLiteral("config"),
                                                       QStringLiteral("--local")} + args);
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
        emit historyEditFinished(verb, false, false,
                                 QObject::tr("Could not set the account in %1.")
                                     .arg(failed.join(QStringLiteral(", "))));
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

void RepoWorker::runCheckout(const QString& ref, int kindValue, int changesValue,
                             bool updateSubmodules) {
    using Kind = RepoSession::RefKind;
    const auto kind = static_cast<Kind>(kindValue);

    // The branch being arrived at, and the switch that gets there. A remote
    // branch becomes a local one tracking it — `git switch origin/x` is refused
    // outright — unless a local one of that name exists already.
    QString target = ref;
    QStringList args{QStringLiteral("switch")};
    if (kind == Kind::Remote) {
        const QString local = ref.section(QChar('/'), 1);
        if (repo_ && localBranchExists(local)) {
            args << local;
        } else {
            args << QStringLiteral("--track") << ref;
        }
        target = local;
    } else if (kind == Kind::Detached) {
        args << QStringLiteral("--detach") << ref;
    } else {
        args << ref;
    }
    runSwitch(QObject::tr("Checkout"), args, target, changesValue, updateSubmodules);
}

void RepoWorker::runCreateBranch(const QString& name, const QString& startPoint, int kindValue,
                                 bool checkout, int changesValue, bool updateSubmodules) {
    using Kind = RepoSession::RefKind;
    const auto kind = static_cast<Kind>(kindValue);
    QStringList args;
    args << (checkout ? QStringLiteral("switch") : QStringLiteral("branch"));
    if (checkout) {
        args << QStringLiteral("-c");
    }
    args << name;
    // Started from someone else's branch under a new name, it is a new line of
    // work, not a copy of theirs: tracking origin/develop would make Pull
    // bring develop in and Push refuse the mismatched name. Push publishes it
    // under its own name instead. Named the same, tracking is the point.
    if (kind == Kind::Remote && startPoint.section(QChar('/'), 1) != name) {
        args << QStringLiteral("--no-track");
    }
    if (!startPoint.isEmpty()) {
        args << startPoint;
    }
    if (!checkout) {
        runBranchCommand(QObject::tr("Create branch"), args);
        return;
    }
    runSwitch(QObject::tr("Create branch"), args, name, changesValue, updateSubmodules);
}

bool RepoWorker::localBranchExists(const QString& name) const {
    return GitProcess::run(workdir_, {QStringLiteral("show-ref"), QStringLiteral("--verify"),
                                      QStringLiteral("--quiet"),
                                      QStringLiteral("refs/heads/%1").arg(name)})
        .ok();
}

void RepoWorker::runSwitch(const QString& verb, QStringList args, const QString& target,
                           int changesValue, bool updateSubmodules) {
    using Changes = RepoSession::LocalChanges;
    const auto changes = static_cast<Changes>(changesValue);
    if (!repo_) {
        emit historyEditFinished(verb, false, false, QObject::tr("No repository is open."));
        return;
    }
    RepoState undoBefore = beginUndo();
    const auto finish = [&](bool ok, const QString& message) {
        endUndo(verb, std::move(undoBefore), true);
        emit historyEditFinished(verb, ok, false, message);
        reportOperation();
        refreshStatus();
    };

    if (changes == Changes::Discard) {
        // Tracked files only: untracked ones are not part of either branch and
        // are left where they are.
        args.insert(1, QStringLiteral("--discard-changes"));
    }

    bool stashed = false;
    if (changes == Changes::StashAndReapply) {
        const GitResult stash = GitProcess::run(
            workdir_, {QStringLiteral("stash"), QStringLiteral("push"),
                       QStringLiteral("--include-untracked"), QStringLiteral("--message"),
                       QObject::tr("Gity: before switching to %1").arg(target)});
        if (!stash.ok()) {
            finish(false, QObject::tr("Could not stash the changes: %1").arg(stash.firstProblemLine()));
            return;
        }
        stashed = !stash.output.contains(QStringLiteral("No local changes"));
    }

    const GitResult switched = GitProcess::run(workdir_, args, 300000);
    if (!switched.ok()) {
        // Nothing switched: the stash goes straight back where it came from.
        if (stashed) {
            static_cast<void>(GitProcess::run(workdir_, {QStringLiteral("stash"), QStringLiteral("pop")}));
        }
        finish(false, switched.firstProblemLine());
        return;
    }

    QStringList notes;
    if (updateSubmodules) {
        const GitResult submodules = GitProcess::run(
            workdir_, {QStringLiteral("submodule"), QStringLiteral("update"), QStringLiteral("--init"),
                       QStringLiteral("--recursive")},
            600000);
        notes << (submodules.ok()
                      ? QObject::tr("submodules moved to the commits it records")
                      : QObject::tr("a submodule could not be updated: %1")
                            .arg(submodules.firstProblemLine()));
    }
    if (stashed) {
        const GitResult popped =
            GitProcess::run(workdir_, {QStringLiteral("stash"), QStringLiteral("pop")});
        notes << (popped.ok() ? QObject::tr("your changes reapplied")
                              : QObject::tr("reapplying your changes conflicted — resolve them; "
                                            "the stash is kept until you do"));
    }
    finish(true, notes.isEmpty()
                     ? QObject::tr("Switched to %1.").arg(target)
                     : QObject::tr("Switched to %1: %2.").arg(target, notes.join(QStringLiteral("; "))));
}

void RepoWorker::refreshStatus() {
    if (!repo_) {
        return;
    }
    try {
        emit statusReady(std::make_shared<const gity::git::WorkingCopyStatus>(
            gity::git::loadStatus(repo_.get())));
    } catch (const gity::git::GitException& e) {
        // Not silent. A status that cannot be read leaves the working-copy
        // screen showing what was there before, and without this the only
        // symptom is a screen that quietly stops updating.
        qWarning("gity: could not read the working copy status: %s", e.error().message.c_str());
    }
    // Whether a merge or rebase is underway changes for the same reasons the
    // status does — a commit concluding it, an abort, a resolution. Reporting
    // it here means every path that refreshes status keeps the banner honest,
    // rather than each caller having to remember.
    reportOperation();
}

void RepoWorker::stage(const QStringList& paths) {
    if (!repo_) {
        return;
    }
    try {
        std::vector<std::string> native;
        native.reserve(static_cast<std::size_t>(paths.size()));
        for (const QString& path : paths) {
            native.push_back(path.toStdString());
        }
        gity::git::stageFiles(repo_.get(), native);
        refreshStatus();
    } catch (const gity::git::GitException& e) {
        emit commitFailed(QString::fromStdString(e.error().message),
                          QString::fromStdString(e.error().raw));
    }
}

void RepoWorker::unstage(const QStringList& paths) {
    if (!repo_) {
        return;
    }
    try {
        std::vector<std::string> native;
        native.reserve(static_cast<std::size_t>(paths.size()));
        for (const QString& path : paths) {
            native.push_back(path.toStdString());
        }
        gity::git::unstageFiles(repo_.get(), native);
        refreshStatus();
    } catch (const gity::git::GitException& e) {
        emit commitFailed(QString::fromStdString(e.error().message),
                          QString::fromStdString(e.error().raw));
    }
}

void RepoWorker::loadWorkingFileDiff(const QString& path, bool staged, quint64 generation) {
    if (!repo_) {
        return;
    }
    try {
        auto diff = std::make_shared<const gity::git::FileDiff>(gity::git::loadWorkingDiff(
            repo_.get(), path.toStdString(),
            staged ? gity::git::DiffSide::Staged : gity::git::DiffSide::Unstaged));

        // Only the unstaged side can be partly staged. On the staged side
        // every line shown is staged already, so the count would say nothing.
        gity::session::HunkStagingPtr staging;
        if (!staged && !diff->hunks.empty()) {
            const gity::git::FileDiff combined = gity::git::loadWorkingDiff(
                repo_.get(), path.toStdString(), gity::git::DiffSide::Combined);
            staging = std::make_shared<const std::vector<gity::git::HunkStaging>>(
                gity::git::hunkStagingCounts(*diff, combined));
        }
        emit workingDiffReady(std::move(diff), std::move(staging), generation);
    } catch (const gity::git::GitException& e) {
        qWarning("gity: could not diff %s: %s", qPrintable(path), e.error().message.c_str());
    }
}

void RepoWorker::applyPatch(const QString& patchText) {
    if (!repo_ || patchText.isEmpty()) {
        return;
    }
    try {
        gity::git::applyPatchToIndex(repo_.get(), patchText.toStdString());
        refreshStatus();
    } catch (const gity::git::GitException& e) {
        emit commitFailed(QString::fromStdString(e.error().message),
                          QString::fromStdString(e.error().raw));
    }
}

void RepoWorker::discardPatch(const QString& patchText) {
    if (!repo_ || patchText.isEmpty()) {
        return;
    }
    RepoState undoBefore = beginUndo();
    try {
        gity::git::applyPatchToWorktree(repo_.get(), patchText.toStdString());
        endUndo(QObject::tr("Discard hunk"), std::move(undoBefore), true);
        refreshStatus();
    } catch (const gity::git::GitException& e) {
        emit commitFailed(QString::fromStdString(e.error().message),
                          QString::fromStdString(e.error().raw));
    }
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
    const GitResult configured = GitProcess::run(
        dir, {QStringLiteral("config"), QStringLiteral("--get-regexp"),
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
            const GitResult listed = GitProcess::run(
                workdir_, {QStringLiteral("submodule"), QStringLiteral("foreach"),
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
        const QString summary = outcome.summary.empty()
                                    ? QObject::tr("already up to date")
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
        {QStringLiteral("clone"), QStringLiteral("--progress"), QStringLiteral("--"), url,
         folder},
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
    emit remotesReady(
        std::make_shared<const std::vector<gity::git::RemoteEntry>>(
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

void RepoWorker::reportOperation() {
    if (!repo_) {
        emit operationChanged(static_cast<int>(gity::git::Operation::None), {});
        return;
    }
    // git_repository_path is the .git directory, which is where the markers
    // live — not the working directory, and not the same thing in a worktree
    // or a submodule.
    const char* path = git_repository_path(repo_.get());
    if (path == nullptr) {
        emit operationChanged(static_cast<int>(gity::git::Operation::None), {});
        return;
    }
    const gity::git::Operation operation = gity::git::detectOperation(path);
    const QDir own(gityDir());
    if (operation == gity::git::Operation::None) {
        // Left over from a rebase that has ended, however it ended.
        if (own.exists(QStringLiteral("reword"))) {
            QDir(own.filePath(QStringLiteral("reword"))).removeRecursively();
        }
        clearEditStop();
    }
    const QString editing =
        own.exists(QStringLiteral("edit-stop-message"))
            ? readTrimmed(own.filePath(QStringLiteral("edit-stop-message"))).section(QChar('\n'), 0, 0)
            : QString();
    emit editStopChanged(editing);
    emit operationChanged(static_cast<int>(operation),
                          QString::fromUtf8(gity::git::operationNoun(operation).data(),
                                            static_cast<qsizetype>(
                                                gity::git::operationNoun(operation).size())));
}

void RepoWorker::runHistoryEdit(const QString& verb, const QStringList& args) {
    if (!repo_) {
        emit historyEditFinished(verb, false, false, QObject::tr("No repository is open."));
        return;
    }

    RepoState undoBefore = beginUndo();

    // GIT_EDITOR=true: `merge --continue` and `rebase --continue` both open
    // the commit message in an editor, and nobody can see one launched from
    // here. A terminal editor would sit on a closed stdin until the timeout;
    // a GUI one pops up out of nowhere. `true` accepts the message git has
    // already prepared — the merge's, or the commit's being replayed.
    const GitResult result = GitProcess::runWithEnv(
        workdir_, args, {{QStringLiteral("GIT_EDITOR"), QStringLiteral("true")}}, 300000);

    // Re-read the state before reporting: whether the repository is now
    // mid-operation is the thing that decides what the UI must offer, and it
    // is a fact on disk rather than an inference from an exit code.
    const char* path = git_repository_path(repo_.get());
    const gity::git::Operation operation =
        path != nullptr ? gity::git::detectOperation(path) : gity::git::Operation::None;
    // State first, exit code second. A verb that stops for the user can exit
    // 0 — `rebase -i` does exactly that at an `edit` step — so trusting the
    // exit code reports "complete" while the repository sits mid-operation and
    // the banner contradicts the status bar.
    // A bisect is not something these verbs start or finish; it can be running
    // underneath any of them, and counting it would report every merge made
    // during one as "stopped".
    const bool stopped = operation != gity::git::Operation::None &&
                         operation != gity::git::Operation::Bisect;

    if (result.ok() && !stopped) {
        // One line, because the status bar shows one. Which line depends on
        // how git wrote it:
        //
        //   * with carriage returns, it was overwriting progress in place and
        //     the *result* is what it left last — `git rebase` emits
        //     "Rebasing (1/1)\rSuccessfully rebased and updated ...", so
        //     taking the first line reports progress as the outcome;
        //   * without them, the first line is the summary and the rest is
        //     detail — `git cherry-pick` writes "[main abc] message" followed
        //     by a date and a file count.
        const QString& raw = result.output.isEmpty() ? result.errorOutput : result.output;
        const std::vector<std::string> lines = gity::git::splitGitOutput(raw.toStdString());
        const bool overwritten = raw.contains(QChar('\r'));
        QString summary;
        if (!lines.empty()) {
            summary = QString::fromStdString(overwritten ? lines.back() : lines.front());
        }
        // A --continue that finished the operation: the last thing git printed
        // is whatever the final step said — a commit line from a reword, say —
        // not that it is over, which is the news.
        if (args.contains(QStringLiteral("--continue"))) {
            summary = QObject::tr("Done — the %1 is complete.").arg(args.value(0));
        }
        emit historyEditFinished(verb, true, false, summary);
    } else if (stopped) {
        // Not a failure. These verbs stopping for a human is their designed
        // behaviour, and calling it an error trains people to ignore it.
        emit historyEditFinished(
            verb, false, true,
            QObject::tr("%1 stopped and needs you. Resolve anything outstanding and stage the "
                        "result, then continue — or abandon it to return to where you were.")
                .arg(verb));
    } else {
        emit historyEditFinished(verb, false, false, result.firstProblemLine());
    }

    endUndo(verb, std::move(undoBefore), result.ok() || stopped);
    reportOperation();
    refreshStatus();
}

QString RepoWorker::publishRemote(QString* problem) const {
    // origin unless the repository has exactly one other remote; guessing
    // beyond that would publish somewhere the user did not choose.
    const std::vector<gity::git::RemoteEntry> remotes = gity::git::listRemotes(repo_.get());
    const bool hasOrigin = std::any_of(
        remotes.begin(), remotes.end(),
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
            const QByteArray keys[] = {
                QStringLiteral("branch.%1.pushRemote").arg(branch).toUtf8(),
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
            runNetwork(verb, {QStringLiteral("push"), QStringLiteral("--progress"), pushRemote,
                              branch});
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

QString RepoWorker::gityDir() const {
    const char* path = repo_ ? git_repository_path(repo_.get()) : nullptr;
    return path != nullptr ? QDir(QString::fromUtf8(path)).filePath(QStringLiteral("gity"))
                           : QString();
}

QString RepoWorker::prepareEditStop() {
    // An `edit` stop leaves the commit made and HEAD on it; git records which
    // in rebase-merge/amend. To edit it, its changes come back out into the
    // index — staged, where Local Changes shows them — and the commit is
    // made again from there. Only at a clean edit stop: a conflict stop has
    // work in progress that this must not touch.
    const char* path = git_repository_path(repo_.get());
    if (path == nullptr) {
        return {};
    }
    const QDir gitDir(QString::fromUtf8(path));
    const QString stopped = readTrimmed(gitDir.filePath(QStringLiteral("rebase-merge/amend")));
    if (stopped.isEmpty()) {
        return {};
    }
    const GitProcess::InternalScope internal;
    const GitResult head = GitProcess::run(workdir_, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    if (!head.ok() || head.output.trimmed() != stopped) {
        return {}; // already prepared, or the user moved on
    }
    const GitResult unmerged = GitProcess::run(workdir_, {QStringLiteral("ls-files"), QStringLiteral("-u")});
    const GitResult parent = GitProcess::run(
        workdir_, {QStringLiteral("rev-parse"), QStringLiteral("-q"), QStringLiteral("--verify"),
                   QStringLiteral("HEAD^")});
    if (!unmerged.output.trimmed().isEmpty() || !parent.ok()) {
        return {}; // a conflict, or a root commit with nothing to reset to
    }
    const GitResult who = GitProcess::run(
        workdir_, {QStringLiteral("show"), QStringLiteral("-s"), QStringLiteral("--date=raw"),
                   QStringLiteral("--format=%an%n%ae%n%ad"), QStringLiteral("HEAD")});
    const GitResult message = GitProcess::run(
        workdir_, {QStringLiteral("show"), QStringLiteral("-s"), QStringLiteral("--format=%B"),
                   QStringLiteral("HEAD")});
    if (!who.ok() || !message.ok()) {
        return {};
    }
    {
        const GitProcess::VisibleScope visible;
        if (!GitProcess::run(workdir_, {QStringLiteral("reset"), QStringLiteral("--soft"),
                                        QStringLiteral("HEAD^")})
                 .ok()) {
            return {};
        }
    }
    // Who made it and when, so committing it again keeps both.
    QDir().mkpath(gityDir());
    QFile marker(QDir(gityDir()).filePath(QStringLiteral("edit-stop")));
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        marker.write((stopped + QChar('\n') + who.output.trimmed() + QChar('\n')).toUtf8());
    }
    QFile text(QDir(gityDir()).filePath(QStringLiteral("edit-stop-message")));
    if (text.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        text.write(message.output.toUtf8());
    }
    const QString body = message.output.trimmed();
    emit editStopPrepared(body, stopped.left(8));
    return body.section(QChar('\n'), 0, 0);
}

QStringList RepoWorker::editStopAuthorArgs() const {
    // Lines: stopped oid, author name, author email, raw date.
    const QStringList lines =
        readTrimmed(QDir(gityDir()).filePath(QStringLiteral("edit-stop"))).split(QChar('\n'));
    if (lines.size() < 4) {
        return {};
    }
    return {QStringLiteral("--author=%1 <%2>").arg(lines.at(1), lines.at(2)),
            QStringLiteral("--date=%1").arg(lines.at(3))};
}

void RepoWorker::clearEditStop() {
    QFile::remove(QDir(gityDir()).filePath(QStringLiteral("edit-stop")));
    QFile::remove(QDir(gityDir()).filePath(QStringLiteral("edit-stop-message")));
}

void RepoWorker::continueOperation(const QString& message) {
    if (!repo_) {
        return;
    }
    const char* path = git_repository_path(repo_.get());
    const gity::git::Operation operation =
        path != nullptr ? gity::git::detectOperation(path) : gity::git::Operation::None;
    const std::string_view command = gity::git::operationCommand(operation);
    if (command.empty()) {
        return;
    }

    // At a prepared edit stop the commit is staged, not made: git will not
    // continue past staged changes, so make it — with its own author and
    // date, and the message as it stands in the commit box.
    const QStringList author = editStopAuthorArgs();
    if (!author.isEmpty()) {
        const bool staged = !GitProcess::run(workdir_, {QStringLiteral("diff"), QStringLiteral("--cached"),
                                                        QStringLiteral("--quiet")})
                                 .ok();
        if (staged) {
            QString text = message.trimmed();
            if (text.isEmpty()) {
                text = readTrimmed(QDir(gityDir()).filePath(QStringLiteral("edit-stop-message")));
            }
            QStringList args{QStringLiteral("commit"), QStringLiteral("-F"), QStringLiteral("-")};
            args << author;
            const GitResult made = GitProcess::runWithInput(workdir_, args, text.toUtf8());
            if (!made.ok()) {
                emit historyEditFinished(QStringLiteral("Continue"), false, false,
                                         made.firstProblemLine());
                refreshStatus();
                return;
            }
        }
        clearEditStop();
    }

    runHistoryEdit(QStringLiteral("Continue"),
                   {QString::fromUtf8(command.data(), static_cast<qsizetype>(command.size())),
                    QStringLiteral("--continue")});
    // The next stop may be another edit.
    if (!prepareEditStop().isEmpty()) {
        reportOperation();
        refreshStatus();
    }
}

void RepoWorker::runInteractiveRebase(const QString& baseOid,
                                      const std::vector<gity::git::RebaseStep>& steps) {
    const QString verb = QObject::tr("Rebase");
    if (!repo_) {
        emit historyEditFinished(verb, false, false, QObject::tr("No repository is open."));
        return;
    }

    // git runs $GIT_SEQUENCE_EDITOR with the todo file as its argument, so a
    // command that copies our file over it replaces the editor entirely. This
    // is the supported way to drive `rebase -i` without a terminal.
    QTemporaryDir directory;
    if (!directory.isValid()) {
        emit historyEditFinished(verb, false, false,
                                 QObject::tr("Could not create a temporary directory."));
        return;
    }
    const QString todoPath = directory.filePath(QStringLiteral("todo"));

    QFile todo(todoPath);
    if (!todo.open(QIODevice::WriteOnly)) {
        emit historyEditFinished(verb, false, false,
                                 QObject::tr("Could not write the rebase instructions."));
        return;
    }
    // Reword messages go where they outlive this command: a stop earlier in
    // the list resumes later, from a different process, and the step reads
    // its file then. Cleared once no rebase is running (reportOperation).
    std::vector<gity::git::RebaseStep> prepared = steps;
    const QDir rewordDir(QDir(gityDir()).filePath(QStringLiteral("reword")));
    for (auto& step : prepared) {
        if (step.action != gity::git::RebaseAction::Reword) {
            continue;
        }
        if (step.message.empty()) {
            step.action = gity::git::RebaseAction::Pick; // nothing to change
            continue;
        }
        QDir().mkpath(rewordDir.path());
        const QString file = rewordDir.filePath(QString::fromStdString(step.oid));
        QFile out(file);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit historyEditFinished(verb, false, false,
                                     QObject::tr("Could not save the new message for %1.")
                                         .arg(QString::fromStdString(step.oid).left(8)));
            return;
        }
        out.write(QByteArray::fromStdString(step.message + "\n"));
        step.messageFile = QDir::toNativeSeparators(file).toStdString();
    }
    const std::string contents = gity::git::serializeTodo(prepared);
    todo.write(contents.c_str(), static_cast<qint64>(contents.size()));
    todo.close();

    const QList<GitProcess::EnvOverride> overrides{
        {QStringLiteral("GIT_SEQUENCE_EDITOR"),
         QStringLiteral("cp '%1'").arg(todoPath)},
        // A squash asks for the combined message. `true` accepts git's default,
        // which is both messages concatenated — nothing is lost, and no editor
        // opens that the user cannot see.
        {QStringLiteral("GIT_EDITOR"), QStringLiteral("true")},
    };

    RepoState undoBefore = beginUndo();
    const GitResult result = GitProcess::runWithEnv(
        workdir_, {QStringLiteral("rebase"), QStringLiteral("-i"), baseOid}, overrides, 600000);

    const char* path = git_repository_path(repo_.get());
    const gity::git::Operation operation =
        path != nullptr ? gity::git::detectOperation(path) : gity::git::Operation::None;
    // As above: `rebase -i` exits 0 when it stops at an `edit` step, so only
    // the absence of a rebase directory means the rebase actually finished.
    // A bisect is not something these verbs start or finish; it can be running
    // underneath any of them, and counting it would report every merge made
    // during one as "stopped".
    const bool stopped = operation != gity::git::Operation::None &&
                         operation != gity::git::Operation::Bisect;

    const QString editing = stopped ? prepareEditStop() : QString();
    if (result.ok() && !stopped) {
        emit historyEditFinished(verb, true, false, QObject::tr("Rebase complete."));
    } else if (!editing.isEmpty()) {
        emit historyEditFinished(
            verb, true, false,
            QObject::tr("Stopped to edit \"%1\": its changes are staged in Local Changes. "
                        "Change them, then Continue.")
                .arg(editing));
    } else if (stopped) {
        emit historyEditFinished(
            verb, false, true,
            QObject::tr("The rebase stopped. Resolve anything outstanding and continue — or "
                        "abandon it to return the branch to where it was."));
    } else {
        emit historyEditFinished(verb, false, false, result.firstProblemLine());
    }

    endUndo(verb, std::move(undoBefore), result.ok() || stopped);
    reportOperation();
    refreshStatus();
}

void RepoWorker::loadCommitsSince(const QString& baseOid) {
    if (!repo_) {
        return;
    }
    // A NUL between the id and the subject: a commit subject can contain
    // anything, including whatever character seemed safe to split on.
    //
    // The same list git's own todo would hold: topological, not by date — a
    // clock skewed between two machines otherwise reorders commits the user
    // never touched — and without merges, which `rebase -i` flattens rather
    // than picks. A `pick` of a merge stops the rebase partway with an error.
    const GitResult result = GitProcess::run(
        workdir_, {QStringLiteral("log"), QStringLiteral("--topo-order"),
                   QStringLiteral("--no-merges"), QStringLiteral("--format=%H%x00%s"),
                   QStringLiteral("%1..HEAD").arg(baseOid)});
    if (!result.ok()) {
        emit commitsSinceReady(baseOid, {}, {});
        return;
    }

    QStringList oids;
    QStringList subjects;
    for (const QString& line : result.output.split(QChar('\n'), Qt::SkipEmptyParts)) {
        const qsizetype separator = line.indexOf(QChar('\0'));
        if (separator < 0) {
            continue;
        }
        oids << line.left(separator);
        subjects << line.mid(separator + 1);
    }
    emit commitsSinceReady(baseOid, oids, subjects);
}


void RepoWorker::runBranchCommand(const QString& verb, const QStringList& args) {
    if (!repo_) {
        emit historyEditFinished(verb, false, false, QObject::tr("No repository is open."));
        return;
    }

    // Checkout can take a while on a large repository: LFS smudge filters run and
    // thousands of files may be rewritten.
    RepoState undoBefore = beginUndo();
    const GitResult result = GitProcess::run(workdir_, args, 300000);
    endUndo(verb, std::move(undoBefore), result.ok());

    if (result.ok()) {
        const std::vector<std::string> lines = gity::git::splitGitOutput(
            result.output.isEmpty() ? result.errorOutput.toStdString()
                                    : result.output.toStdString());
        emit historyEditFinished(verb, true, false,
                                 lines.empty() ? QString()
                                               : QString::fromStdString(lines.front()));
    } else {
        emit historyEditFinished(verb, false, false, result.firstProblemLine());
    }

    reportOperation();
    refreshStatus();
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
                                 lines.empty() ? QString()
                                               : QString::fromStdString(lines.front()));
    }
    loadLocks();
}

void RepoWorker::discardPaths(const QStringList& paths) {
    if (!repo_ || paths.isEmpty()) {
        return;
    }

    QStringList expanded = paths;
    expanded.removeDuplicates();

    // Tracked and untracked need different verbs: restore returns a tracked
    // file to the commit, but an untracked one has no committed state to
    // return to and can only be deleted.
    QStringList tracked;
    QStringList untracked;
    try {
        // Once, not once per path: a status of a large repository costs
        // about a second, and this runs on every discard.
        const gity::git::WorkingCopyStatus status = gity::git::loadStatus(repo_.get());
        for (const QString& path : expanded) {
            const gity::git::StatusEntry* entry = status.find(path.toStdString());
            if (entry == nullptr) {
                tracked << path;
                continue;
            }
            // A rename not yet staged is, to git, a deleted file and a new
            // one it has never seen: the new path is removed like any
            // untracked file and the old one restored. Handing `restore` the
            // new path made it refuse the whole list.
            const bool unknownToGit =
                entry->isUntracked() ||
                (entry->unstaged == gity::git::FileState::Renamed && !entry->hasStaged());
            (unknownToGit ? untracked : tracked) << path;
            if (!entry->oldPath.empty()) {
                tracked << QString::fromStdString(entry->oldPath);
            }
        }
        tracked.removeDuplicates();
    } catch (const gity::git::GitException&) {
        // Without a status every path is treated as tracked; `restore` will
        // say so itself for anything that is not.
        tracked = expanded;
    }

    RepoState undoBefore = beginUndo();
    if (!tracked.isEmpty()) {
        // --literal-pathspecs: these are file names, not patterns. Read as a
        // glob, discarding `[id].tsx` also reverts `d.tsx` — measured, and
        // with nothing to undo it.
        QStringList args{QStringLiteral("--literal-pathspecs"), QStringLiteral("restore"),
                         QStringLiteral("--source=HEAD"),
                         QStringLiteral("--staged"), QStringLiteral("--worktree"),
                         QStringLiteral("--")};
        args += tracked;
        const GitResult result = GitProcess::run(workdir_, args, 120000);
        if (!result.ok()) {
            emit historyEditFinished(QObject::tr("Discard"), false, false,
                                     result.firstProblemLine());
            refreshStatus();
            return;
        }
    }

    // Reported, not ignored: a file that could not be deleted is still there,
    // and saying "discarded" about it would be false. A directory is never
    // removed wholesale — status lists one only when it holds a repository
    // of its own, whose history `git clean` also refuses to delete.
    QStringList stuck;
    for (const QString& path : untracked) {
        if (!QFile::remove(QDir(workdir_).filePath(path))) {
            stuck << path;
        }
    }
    if (!stuck.isEmpty()) {
        emit historyEditFinished(QObject::tr("Discard"), false, false,
                                 QObject::tr("Could not delete %1").arg(stuck.join(", ")));
        refreshStatus();
        return;
    }

    endUndo(QObject::tr("Discard"), std::move(undoBefore), true);
    emit historyEditFinished(QObject::tr("Discard"), true, false,
                             QObject::tr("Discarded changes to %n file(s)", nullptr,
                                         static_cast<int>(expanded.size())));
    refreshStatus();
}

namespace {

QString humanSize(qint64 bytes) {
    const double value = static_cast<double>(bytes);
    if (bytes >= 1024LL * 1024) {
        return QStringLiteral("%1 MB").arg(value / (1024.0 * 1024.0), 0, 'f', 1);
    }
    if (bytes >= 1024) {
        return QStringLiteral("%1 KB").arg(value / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

/// Fills one side from bytes we already have. Reports what the file actually
/// says; anything needing a decoder we do not have is left out rather than guessed.
void fillSide(ImageSide& side, const gity::git::BlobContent& content, const QString& label) {
    side.label = label;
    side.present = !content.empty();
    side.lfsPointer = content.lfsPointer;
    side.lfsOid = QString::fromStdString(content.lfsOid);
    side.byteSize = content.lfsPointer ? static_cast<qint64>(content.lfsSize)
                                       : static_cast<qint64>(content.size());

    if (!side.present) {
        side.metadata = QObject::tr("not present");
        return;
    }
    if (content.lfsPointer) {
        // The pointer is all we have without `git lfs pull`, so say that
        // rather than rendering 130 bytes of text as a broken image.
        side.metadata = QObject::tr("LFS pointer · %1 · object not fetched")
                            .arg(humanSize(side.byteSize));
        return;
    }

    const QByteArray raw(content.bytes.data(), static_cast<qsizetype>(content.size()));
    side.decoded = side.image.loadFromData(raw);
    if (!side.decoded) {
        side.metadata = QObject::tr("%1 · not a decodable image").arg(humanSize(side.byteSize));
        return;
    }
    side.metadata = QObject::tr("%1 × %2 · %3-bit · %4")
                        .arg(side.image.width())
                        .arg(side.image.height())
                        .arg(side.image.depth())
                        .arg(humanSize(side.byteSize));
}

double changedPercent(const QImage& a, const QImage& b) {
    if (a.size() != b.size() || a.isNull()) {
        return 0.0;
    }
    // Sampled rather than exhaustive: a 4096² pair is 16M pixels and the
    // caption only needs one decimal place.
    const int step = std::max(1, a.width() / 512);
    qint64 sampled = 0;
    qint64 differing = 0;
    for (int y = 0; y < a.height(); y += step) {
        for (int x = 0; x < a.width(); x += step) {
            ++sampled;
            if (a.pixel(x, y) != b.pixel(x, y)) {
                ++differing;
            }
        }
    }
    return sampled == 0 ? 0.0 : (100.0 * static_cast<double>(differing) /
                                 static_cast<double>(sampled));
}

} // namespace

void RepoWorker::loadImageDiff(const QString& path, const QString& commitOid,
                               const QString& baseOid, quint64 generation) {
    if (!repo_) {
        return;
    }
    try {
        auto diff = std::make_shared<ImageDiff>();
        diff->path = path;

        const std::string native = path.toStdString();
        git_oid commit{};
        if (!commitOid.isEmpty() &&
            git_oid_fromstr(&commit, commitOid.toLatin1().constData()) == 0) {
            // What this commit did to the file: its first parent against it.
            // Reading HEAD and the working copy here showed an old commit's
            // image as whatever it looks like today.
            gity::git::BlobContent before;
            QString beforeLabel = QObject::tr("Before");
            gity::git::CommitHandle handle;
            git_oid base{};
            if (!baseOid.isEmpty() &&
                git_oid_fromstr(&base, baseOid.toLatin1().constData()) == 0) {
                // A comparison: the other tip, not the parent.
                before = gity::git::readBlobAt(repo_.get(), base, native);
                beforeLabel = baseOid.left(8);
            } else if (git_commit_lookup(handle.receive(), repo_.get(), &commit) == 0 &&
                       git_commit_parentcount(handle.get()) > 0) {
                const git_oid* parent = git_commit_parent_id(handle.get(), 0);
                before = gity::git::readBlobAt(repo_.get(), *parent, native);
                beforeLabel = QString::fromLatin1(git_oid_tostr_s(parent)).left(8);
            }
            fillSide(diff->before, before, beforeLabel);
            fillSide(diff->after, gity::git::readBlobAt(repo_.get(), commit, native),
                     commitOid.left(8));
        } else {
            fillSide(diff->before, gity::git::readBlobAtHead(repo_.get(), native),
                     QObject::tr("HEAD"));
            fillSide(diff->after, gity::git::readWorktreeFile(workdir_.toStdString(), native),
                     QObject::tr("Working copy"));
        }

        if (diff->comparable()) {
            diff->changedPixelPercent = changedPercent(diff->before.image, diff->after.image);
        }
        emit imageDiffReady(diff, generation);
    } catch (const gity::git::GitException&) {
    }
}

void RepoWorker::commit(const QString& message, bool amend) {
    if (!repo_) {
        return;
    }
    // ADR-003: the commit itself goes through the git binary, so hooks,
    // signing, and the user's config behave exactly as in their terminal.
    // The message goes on stdin — never through a command line.
    QStringList args{QStringLiteral("commit"), QStringLiteral("-F"), QStringLiteral("-")};
    if (amend) {
        args << QStringLiteral("--amend");
    }
    // Recommitting a commit being edited mid-rebase: it is still that
    // person's commit, from that day.
    const QStringList editAuthor = amend ? QStringList() : editStopAuthorArgs();
    args << editAuthor;
    RepoState undoBefore = beginUndo();
    const GitResult result = GitProcess::runWithInput(workdir_, args, message.toUtf8());
    if (result.ok() && !editAuthor.isEmpty()) {
        clearEditStop();
        reportOperation();
    }
    endUndo(amend ? QObject::tr("Amend") : QObject::tr("Commit"), std::move(undoBefore),
            result.ok());

    if (result.ok()) {
        const GitResult described =
            GitProcess::run(workdir_, {QStringLiteral("log"), QStringLiteral("-1"),
                                       QStringLiteral("--format=%h %s")});
        emit committed(described.ok() ? described.output.trimmed() : QString());
    } else {
        emit commitFailed(result.firstProblemLine(), result.errorOutput);
    }
}

RepoSession::RepoSession(QObject* parent) : QObject(parent) {
    qRegisterMetaType<RepoInfo>("gity::session::RepoInfo");
    qRegisterMetaType<ChunkPtr>("gity::session::ChunkPtr");
    qRegisterMetaType<RefSetPtr>("gity::session::RefSetPtr");
    qRegisterMetaType<DetailPtr>("gity::session::DetailPtr");
    qRegisterMetaType<ComparisonPtr>("gity::session::ComparisonPtr");
    qRegisterMetaType<PreviewPtr>("gity::session::PreviewPtr");
    qRegisterMetaType<FileDiffPtr>("gity::session::FileDiffPtr");
    qRegisterMetaType<StatusPtr>("gity::session::StatusPtr");
    qRegisterMetaType<ImageDiffPtr>("gity::session::ImageDiffPtr");

    worker_ = new RepoWorker(cancel_);
    worker_->moveToThread(&thread_);

    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &RepoWorker::opened, this, &RepoSession::opened);
    connect(worker_, &RepoWorker::refsReady, this, &RepoSession::refsReady);
    connect(worker_, &RepoWorker::chunkReady, this, &RepoSession::chunkReady);
    connect(worker_, &RepoWorker::detailReady, this,
            [this](DetailPtr detail, quint64 generation) {
                // Drop results the user has already scrolled past.
                if (generation == detailGeneration_) {
                    emit detailReady(std::move(detail));
                }
            });
    connect(worker_, &RepoWorker::comparisonReady, this,
            [this](ComparisonPtr comparison, quint64 generation) {
                if (generation == detailGeneration_) {
                    emit comparisonReady(std::move(comparison));
                }
            });
    connect(worker_, &RepoWorker::fileDiffReady, this,
            [this](FileDiffPtr diff, quint64 generation) {
                if (generation == diffGeneration_) {
                    emit fileDiffReady(std::move(diff));
                }
            });
    connect(worker_, &RepoWorker::statusReady, this, &RepoSession::statusReady);
    connect(worker_, &RepoWorker::networkStarted, this, &RepoSession::networkStarted);
    connect(worker_, &RepoWorker::networkProgress, this, &RepoSession::networkProgress);
    connect(worker_, &RepoWorker::cloneFinished, this, &RepoSession::cloneFinished);
    connect(worker_, &RepoWorker::remotesReady, this, &RepoSession::remotesReady);
    connect(worker_, &RepoWorker::remoteCommandFailed, this,
            &RepoSession::remoteCommandFailed);
    connect(worker_, &RepoWorker::operationChanged, this, &RepoSession::operationChanged);
    connect(worker_, &RepoWorker::historyEditFinished, this,
            &RepoSession::historyEditFinished);
    connect(worker_, &RepoWorker::commitsSinceReady, this, &RepoSession::commitsSinceReady);
    connect(worker_, &RepoWorker::editStopPrepared, this, &RepoSession::editStopPrepared);
    connect(worker_, &RepoWorker::editStopChanged, this, &RepoSession::editStopChanged);
    connect(worker_, &RepoWorker::commitPeekReady, this, &RepoSession::commitPeekReady);
    connect(worker_, &RepoWorker::commitPeekDiffReady, this, &RepoSession::commitPeekDiffReady);
    connect(worker_, &RepoWorker::locksReady, this, &RepoSession::locksReady);
    connect(worker_, &RepoWorker::undoStateChanged, this, &RepoSession::undoStateChanged);
    connect(worker_, &RepoWorker::previewReady, this, &RepoSession::previewReady);
    connect(worker_, &RepoWorker::accountChoiceReady, this, &RepoSession::accountChoiceReady);
    connect(worker_, &RepoWorker::networkFinished, this, &RepoSession::networkFinished);
    connect(worker_, &RepoWorker::imageDiffReady, this,
            [this](ImageDiffPtr diff, quint64 generation) {
                if (generation == imageGeneration_) {
                    emit imageDiffReady(std::move(diff));
                }
            });
    connect(worker_, &RepoWorker::workingDiffReady, this,
            [this](FileDiffPtr diff, HunkStagingPtr staging, quint64 generation) {
                if (generation == workingDiffGeneration_) {
                    emit workingDiffReady(std::move(diff), std::move(staging));
                }
            });
    connect(worker_, &RepoWorker::committed, this, &RepoSession::committed);
    connect(worker_, &RepoWorker::commitFailed, this, &RepoSession::commitFailed);
    connect(worker_, &RepoWorker::finished, this, &RepoSession::finished);
    connect(worker_, &RepoWorker::failed, this, &RepoSession::failed);

    thread_.start();
    // Registered on the worker thread itself, since the flag is per thread.
    QMetaObject::invokeMethod(
        worker_, [this] { GitProcess::setThreadAbortFlag(&shutdown_); }, Qt::QueuedConnection);
}

RepoSession::~RepoSession() {
    // Both flags: cancel_ stops a history walk, shutdown_ stops a git process.
    // Without the second, closing a tab mid-fetch froze the window until the
    // fetch finished or hit its ten-minute timeout.
    shutdown_.store(true, std::memory_order_relaxed);
    cancel_.store(true, std::memory_order_relaxed);
    thread_.quit();
    thread_.wait();
}

void RepoSession::open(const QString& path) {
    // Supersede rather than queue behind: cancel the in-flight walk, then let
    // the new request run once the worker returns to the event loop.
    cancel_.store(true, std::memory_order_relaxed);
    QMetaObject::invokeMethod(
        worker_,
        [this, path] {
            cancel_.store(false, std::memory_order_relaxed);
            worker_->open(path);
        },
        Qt::QueuedConnection);
}

void RepoSession::open(const QString& path, const gity::git::CommitFilter& filter) {
    cancel_.store(true, std::memory_order_relaxed);
    QMetaObject::invokeMethod(
        worker_,
        [this, path, filter] {
            cancel_.store(false, std::memory_order_relaxed);
            worker_->setFilter(filter);
            worker_->open(path);
        },
        Qt::QueuedConnection);
}

void RepoSession::requestDetail(const QString& oidHex) {
    const quint64 generation = ++detailGeneration_;
    QMetaObject::invokeMethod(
        worker_, [this, oidHex, generation] { worker_->loadDetail(oidHex, generation); },
        Qt::QueuedConnection);
}

void RepoSession::requestCommitPeek(const QString& oidHex, const QString& path) {
    QMetaObject::invokeMethod(
        worker_, [this, oidHex, path] { worker_->loadCommitPeek(oidHex, path); },
        Qt::QueuedConnection);
}

void RepoSession::requestFileDiff(const QString& oidHex, const QString& path) {
    const quint64 generation = ++diffGeneration_;
    QMetaObject::invokeMethod(
        worker_,
        [this, oidHex, path, generation] { worker_->loadFileDiff(oidHex, path, generation); },
        Qt::QueuedConnection);
}

void RepoSession::requestComparison(const QString& fromHex, const QString& toHex) {
    const quint64 generation = ++detailGeneration_;
    QMetaObject::invokeMethod(
        worker_,
        [this, fromHex, toHex, generation] {
            worker_->loadComparison(fromHex, toHex, generation);
        },
        Qt::QueuedConnection);
}

void RepoSession::requestFileDiffBetween(const QString& fromHex, const QString& toHex,
                                         const QString& path) {
    const quint64 generation = ++diffGeneration_;
    QMetaObject::invokeMethod(
        worker_,
        [this, fromHex, toHex, path, generation] {
            worker_->loadFileDiffBetween(fromHex, toHex, path, generation);
        },
        Qt::QueuedConnection);
}

void RepoSession::requestStatus() {
    QMetaObject::invokeMethod(
        worker_, [this] { worker_->refreshStatus(); }, Qt::QueuedConnection);
}

void RepoSession::requestStage(const QStringList& paths) {
    QMetaObject::invokeMethod(
        worker_, [this, paths] { worker_->stage(paths); }, Qt::QueuedConnection);
}

void RepoSession::requestUnstage(const QStringList& paths) {
    QMetaObject::invokeMethod(
        worker_, [this, paths] { worker_->unstage(paths); }, Qt::QueuedConnection);
}

void RepoSession::requestWorkingDiff(const QString& path, bool staged) {
    const quint64 generation = ++workingDiffGeneration_;
    QMetaObject::invokeMethod(
        worker_,
        [this, path, staged, generation] {
            worker_->loadWorkingFileDiff(path, staged, generation);
        },
        Qt::QueuedConnection);
}

void RepoSession::requestImageDiff(const QString& path, const QString& commitOid,
                                   const QString& baseOid) {
    const quint64 generation = ++imageGeneration_;
    QMetaObject::invokeMethod(
        worker_,
        [this, path, commitOid, baseOid, generation] {
            worker_->loadImageDiff(path, commitOid, baseOid, generation);
        },
        Qt::QueuedConnection);
}

void RepoSession::requestFetch(bool withSubmodules) {
    QMetaObject::invokeMethod(
        worker_,
        [this, withSubmodules] {
            worker_->runFetch(withSubmodules);
        },
        Qt::QueuedConnection);
}

void RepoSession::requestPull() {
    QMetaObject::invokeMethod(
        worker_,
        [this] {
            // --ff-only rather than a merge: a client that silently creates
            // merge commits on pull is how a history fills with merges nobody
            // intended.
            worker_->runNetwork(QStringLiteral("Pull"),
                                {QStringLiteral("pull"), QStringLiteral("--ff-only"),
                                 QStringLiteral("--progress")});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestPush() {
    QMetaObject::invokeMethod(
        worker_, [this] { worker_->runPush(); }, Qt::QueuedConnection);
}

void RepoSession::requestPushRef(const QString& branch) {
    QMetaObject::invokeMethod(
        worker_, [this, branch] { worker_->runPushBranch(branch); }, Qt::QueuedConnection);
}

void RepoSession::requestClone(const QString& url, const QString& parentDir,
                               const QString& folder) {
    QMetaObject::invokeMethod(
        worker_, [this, url, parentDir, folder] { worker_->runClone(url, parentDir, folder); },
        Qt::QueuedConnection);
}

void RepoSession::requestRemotes() {
    QMetaObject::invokeMethod(
        worker_, [this] { worker_->loadRemotes(); }, Qt::QueuedConnection);
}

void RepoSession::requestAddRemote(const QString& name, const QString& url) {
    QMetaObject::invokeMethod(
        worker_,
        [this, name, url] {
            worker_->runRemoteCommand(
                {QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("--"), name, url});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestRemoveRemote(const QString& name) {
    QMetaObject::invokeMethod(
        worker_,
        [this, name] {
            worker_->runRemoteCommand(
                {QStringLiteral("remote"), QStringLiteral("remove"), name});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestRenameRemote(const QString& from, const QString& to) {
    QMetaObject::invokeMethod(
        worker_,
        [this, from, to] {
            worker_->runRemoteCommand(
                {QStringLiteral("remote"), QStringLiteral("rename"), from, to});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestSetRemoteUrl(const QString& name, const QString& url) {
    QMetaObject::invokeMethod(
        worker_,
        [this, name, url] {
            worker_->runRemoteCommand(
                {QStringLiteral("remote"), QStringLiteral("set-url"), QStringLiteral("--"), name,
                 url});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestMerge(const QString& ref) {
    QMetaObject::invokeMethod(
        worker_,
        [this, ref] {
            // --no-ff is not forced: a fast-forward is what the user expects
            // when nothing has diverged. --no-edit keeps git from opening an
            // editor no one would see.
            worker_->runHistoryEdit(QStringLiteral("Merge"),
                                    {QStringLiteral("merge"), QStringLiteral("--no-edit"), ref});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestRebaseOnto(const QString& ref) {
    QMetaObject::invokeMethod(
        worker_,
        [this, ref] {
            worker_->runHistoryEdit(QStringLiteral("Rebase"),
                                    {QStringLiteral("rebase"), ref});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestCherryPick(const QString& oidHex) {
    QMetaObject::invokeMethod(
        worker_,
        [this, oidHex] {
            worker_->runHistoryEdit(
                QStringLiteral("Cherry-pick"),
                {QStringLiteral("cherry-pick"), QStringLiteral("--no-edit"), oidHex});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestRevert(const QString& oidHex) {
    QMetaObject::invokeMethod(
        worker_,
        [this, oidHex] {
            worker_->runHistoryEdit(
                QStringLiteral("Revert"),
                {QStringLiteral("revert"), QStringLiteral("--no-edit"), oidHex});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestContinueOperation(const QString& message) {
    QMetaObject::invokeMethod(
        worker_, [this, message] { worker_->continueOperation(message); }, Qt::QueuedConnection);
}

void RepoSession::requestAbortOperation() {
    QMetaObject::invokeMethod(
        worker_,
        [this] {
            // The verb comes from what is actually on disk. Assuming "merge"
            // here is how a stopped rebase gets abandoned with the wrong
            // command.
            const char* path = git_repository_path(worker_->repository());
            const gity::git::Operation operation =
                path != nullptr ? gity::git::detectOperation(path) : gity::git::Operation::None;
            const std::string_view command = gity::git::operationCommand(operation);
            if (command.empty()) {
                return;
            }
            worker_->clearEditStop();
            worker_->runHistoryEdit(
                QStringLiteral("Abort"),
                {QString::fromUtf8(command.data(), static_cast<qsizetype>(command.size())),
                 QStringLiteral("--abort")});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestFilter(const gity::git::CommitFilter& filter) {
    // Cancels whatever walk is in flight: a filter typed while the previous
    // one is still streaming must not interleave two sets of rows.
    cancel_.store(true, std::memory_order_relaxed);
    QMetaObject::invokeMethod(
        worker_,
        [this, filter] {
            cancel_.store(false, std::memory_order_relaxed);
            worker_->rewalk(filter);
        },
        Qt::QueuedConnection);
}

void RepoSession::requestOperationState() {
    QMetaObject::invokeMethod(
        worker_, [this] { worker_->reportOperation(); }, Qt::QueuedConnection);
}

void RepoSession::requestInteractiveRebase(const QString& baseOid,
                                           const std::vector<git::RebaseStep>& steps) {
    QMetaObject::invokeMethod(
        worker_, [this, baseOid, steps] { worker_->runInteractiveRebase(baseOid, steps); },
        Qt::QueuedConnection);
}

void RepoSession::requestCommitsSince(const QString& baseOid) {
    QMetaObject::invokeMethod(
        worker_, [this, baseOid] { worker_->loadCommitsSince(baseOid); }, Qt::QueuedConnection);
}

void RepoSession::requestCheckout(const QString& ref) {
    QMetaObject::invokeMethod(
        worker_,
        [this, ref] {
            // `switch` rather than `checkout`: it refuses to silently create a
            // detached HEAD or clobber local changes, which `checkout` will do
            // in some of its many modes.
            worker_->runBranchCommand(QObject::tr("Checkout"),
                                      {QStringLiteral("switch"), ref});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestCheckout(const QString& ref, RefKind kind, LocalChanges changes,
                                  bool updateSubmodules) {
    QMetaObject::invokeMethod(
        worker_,
        [this, ref, kind, changes, updateSubmodules] {
            worker_->runCheckout(ref, static_cast<int>(kind), static_cast<int>(changes),
                                 updateSubmodules);
        },
        Qt::QueuedConnection);
}

void RepoSession::requestCreateBranch(const QString& name, const QString& startPoint,
                                      RefKind kind, bool checkout, LocalChanges changes,
                                      bool updateSubmodules) {
    QMetaObject::invokeMethod(
        worker_,
        [this, name, startPoint, kind, checkout, changes, updateSubmodules] {
            worker_->runCreateBranch(name, startPoint, static_cast<int>(kind), checkout,
                                     static_cast<int>(changes), updateSubmodules);
        },
        Qt::QueuedConnection);
}

void RepoSession::requestDeleteBranch(const QString& name, bool force) {
    QMetaObject::invokeMethod(
        worker_,
        [this, name, force] {
            worker_->runBranchCommand(
                QObject::tr("Delete branch"),
                {QStringLiteral("branch"), force ? QStringLiteral("-D") : QStringLiteral("-d"),
                 name});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestCreateTag(const QString& name, const QString& target,
                                   const QString& message) {
    QMetaObject::invokeMethod(
        worker_,
        [this, name, target, message] {
            QStringList args{QStringLiteral("tag")};
            if (!message.isEmpty()) {
                // Annotated: an object of its own with an author and a date.
                args << QStringLiteral("--annotate") << QStringLiteral("--message") << message;
            }
            args << name;
            if (!target.isEmpty()) {
                args << target;
            }
            worker_->runBranchCommand(QObject::tr("Create tag"), args);
        },
        Qt::QueuedConnection);
}

void RepoSession::requestDeleteTag(const QString& name) {
    QMetaObject::invokeMethod(
        worker_,
        [this, name] {
            worker_->runBranchCommand(
                QObject::tr("Delete tag"),
                {QStringLiteral("tag"), QStringLiteral("--delete"), name});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestPushTag(const QString& name) {
    QMetaObject::invokeMethod(
        worker_, [this, name] { worker_->runPushTag(name); }, Qt::QueuedConnection);
}

void RepoSession::requestReset(const QString& target, const QString& mode) {
    QMetaObject::invokeMethod(
        worker_,
        [this, target, mode] {
            worker_->runBranchCommand(
                QObject::tr("Reset"),
                {QStringLiteral("reset"), QStringLiteral("--%1").arg(mode), target});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestStashSave(const QString& message, bool includeUntracked,
                                   const QStringList& paths) {
    QMetaObject::invokeMethod(
        worker_,
        [this, message, includeUntracked, paths] {
            // --literal-pathspecs: file names, not patterns — "[id].tsx" is a
            // file, not a character class.
            QStringList args{QStringLiteral("--literal-pathspecs"), QStringLiteral("stash"),
                             QStringLiteral("push")};
            if (includeUntracked) {
                args << QStringLiteral("--include-untracked");
            }
            if (!message.isEmpty()) {
                args << QStringLiteral("--message") << message;
            }
            if (!paths.isEmpty()) {
                args << QStringLiteral("--") << paths;
            }
            worker_->runBranchCommand(QObject::tr("Stash"), args);
        },
        Qt::QueuedConnection);
}

void RepoSession::requestStashApply(const QString& ref, bool pop) {
    QMetaObject::invokeMethod(
        worker_,
        [this, ref, pop] {
            worker_->runBranchCommand(
                pop ? QObject::tr("Pop stash") : QObject::tr("Apply stash"),
                {QStringLiteral("stash"), pop ? QStringLiteral("pop") : QStringLiteral("apply"),
                 ref});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestStashDrop(const QString& ref) {
    QMetaObject::invokeMethod(
        worker_,
        [this, ref] {
            worker_->runBranchCommand(QObject::tr("Drop stash"),
                                      {QStringLiteral("stash"), QStringLiteral("drop"), ref});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestDiscardPaths(const QStringList& paths) {
    QMetaObject::invokeMethod(
        worker_, [this, paths] { worker_->discardPaths(paths); }, Qt::QueuedConnection);
}

void RepoSession::requestLocks() {
    QMetaObject::invokeMethod(
        worker_, [this] { worker_->loadLocks(); }, Qt::QueuedConnection);
}

void RepoSession::requestLock(const QString& path) {
    QMetaObject::invokeMethod(
        worker_,
        [this, path] {
            worker_->runLockCommand(
                QObject::tr("Lock"),
                {QStringLiteral("lfs"), QStringLiteral("lock"), QStringLiteral("--"), path});
        },
        Qt::QueuedConnection);
}

void RepoSession::requestUnlock(const QString& path, bool force) {
    QMetaObject::invokeMethod(
        worker_,
        [this, path, force] {
            QStringList args{QStringLiteral("lfs"), QStringLiteral("unlock")};
            if (force) {
                args << QStringLiteral("--force");
            }
            args << QStringLiteral("--") << path;
            worker_->runLockCommand(QObject::tr("Unlock"), args);
        },
        Qt::QueuedConnection);
}

void RepoSession::requestDiscardPatch(const QString& patchText) {
    QMetaObject::invokeMethod(
        worker_, [this, patchText] { worker_->discardPatch(patchText); },
        Qt::QueuedConnection);
}

void RepoSession::requestApplyPatch(const QString& patchText) {
    QMetaObject::invokeMethod(
        worker_, [this, patchText] { worker_->applyPatch(patchText); }, Qt::QueuedConnection);
}

void RepoSession::requestCommit(const QString& message, bool amend) {
    QMetaObject::invokeMethod(
        worker_, [this, message, amend] { worker_->commit(message, amend); },
        Qt::QueuedConnection);
}

void RepoSession::requestUndo() {
    QMetaObject::invokeMethod(
        worker_, [this] { worker_->undo(); }, Qt::QueuedConnection);
}

void RepoSession::requestRedo() {
    QMetaObject::invokeMethod(
        worker_, [this] { worker_->redo(); }, Qt::QueuedConnection);
}

void RepoSession::requestPreview(Preview::Kind kind, const QString& target,
                                 const QString& branch) {
    QMetaObject::invokeMethod(
        worker_,
        [this, kind, target, branch] {
            worker_->computePreview(static_cast<int>(kind), target, branch);
        },
        Qt::QueuedConnection);
}

void RepoSession::requestForcePush(const QString& branch, const QString& remote,
                                   const QString& remoteBranch, const QString& leaseOid) {
    QMetaObject::invokeMethod(
        worker_,
        [this, branch, remote, remoteBranch, leaseOid] {
            worker_->runForcePush(branch, remote, remoteBranch, leaseOid);
        },
        Qt::QueuedConnection);
}

void RepoSession::requestAccountChoice() {
    QMetaObject::invokeMethod(
        worker_, [this] { worker_->loadAccountChoice(); }, Qt::QueuedConnection);
}

void RepoSession::requestSetAccount(const QString& account) {
    QMetaObject::invokeMethod(
        worker_, [this, account] { worker_->setAccountChoice(account); }, Qt::QueuedConnection);
}

void RepoSession::cancel() {
    cancel_.store(true, std::memory_order_relaxed);
}

} // namespace gity::session
