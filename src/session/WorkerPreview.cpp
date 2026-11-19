// RepoWorker: what a pull, merge, rebase or push would do, before it runs.
#include "RepoWorker.h"

#include "core/git/MergePrediction.h"

namespace gity::session {

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
            workdir_, {QStringLiteral("rev-parse"), QStringLiteral("-q"),
                       QStringLiteral("--verify"), rev + QStringLiteral("^{commit}")});
        return result.ok() ? trimmedOutput(result) : QString();
    };
    const auto config = [this](const QString& key) {
        return trimmedOutput(
            GitProcess::run(workdir_, {QStringLiteral("config"), QStringLiteral("--get"), key}));
    };

    // The branch acted on: the one named, or the one checked out.
    QString branch = branchName;
    if (branch.isEmpty()) {
        const GitResult head =
            GitProcess::run(workdir_, {QStringLiteral("symbolic-ref"), QStringLiteral("-q"),
                                       QStringLiteral("--short"), QStringLiteral("HEAD")});
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
    // Publishing to `remote`: everything not already on some remote goes.
    const auto publish = [&](const QString& remote) {
        preview->newBranch = true;
        preview->remote = remote;
        preview->remoteBranch = branch;
        preview->target = QStringLiteral("%1/%2").arg(remote, branch);
        const GitResult count = GitProcess::run(
            // "--" last: a branch named like a top-level folder ("docs") is
            // otherwise ambiguous, and git refuses — and the preview said
            // nothing was going out.
            workdir_, {QStringLiteral("rev-list"), QStringLiteral("--count"), branch,
                       QStringLiteral("--not"), QStringLiteral("--remotes"), QStringLiteral("--")});
        preview->outgoing = trimmedOutput(count).toInt();
        const GitResult log = GitProcess::run(
            workdir_,
            {QStringLiteral("log"), QStringLiteral("-10"), QStringLiteral("--format=%h%x00%s"),
             branch, QStringLiteral("--not"), QStringLiteral("--remotes"), QStringLiteral("--")});
        for (const QString& line : log.output.split(QChar('\n'), Qt::SkipEmptyParts)) {
            preview->outgoingCommits << line.section(QChar('\0'), 0, 0).leftJustified(8) +
                                            QStringLiteral("  ") + line.section(QChar('\0'), 1);
        }
        finish();
    };
    // Where a push goes when that is not the upstream's remote — the fork,
    // in a triangular setup — as runPushBranch decides it. The preview has
    // to describe that remote: compared with the upstream instead, a branch
    // the upstream had moved past looked rejected, and the force push
    // offered for it went to the upstream.
    const QString pushTo = isPush ? pushRemoteOf(branch) : QString();
    QString other = target;
    if (isPull || isPush) {
        const GitResult upstream =
            GitProcess::run(workdir_, {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"),
                                       QStringLiteral("--symbolic-full-name"),
                                       branch + QStringLiteral("@{upstream}")});
        preview->remote = config(QStringLiteral("branch.%1.remote").arg(branch));
        preview->remoteBranch = config(QStringLiteral("branch.%1.merge").arg(branch))
                                    .remove(QStringLiteral("refs/heads/"));
        if (!upstream.ok() || preview->remote.isEmpty() || preview->remote == QStringLiteral(".")) {
            if (isPull) {
                preview->problem = QObject::tr("%1 tracks no remote branch, so there is nothing "
                                               "to pull. Push it first to publish it.")
                                       .arg(branch);
                finish();
                return;
            }
            QString problem;
            const QString remote = !pushTo.isEmpty() ? pushTo : publishRemote(&problem);
            if (remote.isEmpty()) {
                preview->remote.clear();
                preview->problem = problem;
                finish();
                return;
            }
            publish(remote);
            return;
        }
        other = trimmedOutput(upstream);
        preview->target = other;
        if (!pushTo.isEmpty() && pushTo != preview->remote) {
            const QString there = QStringLiteral("%1/%2").arg(pushTo, branch);
            if (resolve(QStringLiteral("refs/remotes/") + there).isEmpty()) {
                publish(pushTo); // not on the fork yet
                return;
            }
            preview->remote = pushTo;
            preview->remoteBranch = branch;
            other = there;
            preview->target = there;
        }
    }

    if (isPull) {
        // Of the remote as it is now, not as last fetched: that is what the
        // pull will actually bring in. Progress shows in the status bar; this
        // is not a Fetch the user asked for, so it does not announce one.
        const GitProcess::VisibleScope visible;
        // `--`: the remote's name is read from the config, and must not be
        // taken for an option such as --upload-pack.
        const GitResult fetched = GitProcess::runStreaming(
            workdir_,
            {QStringLiteral("fetch"), QStringLiteral("--progress"), QStringLiteral("--"),
             preview->remote},
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
            workdir_,
            {QStringLiteral("symbolic-ref"), QStringLiteral("-q"), QStringLiteral("--short"),
             QStringLiteral("refs/remotes/%1/HEAD").arg(preview->remote)}));
        preview->shared =
            sharedNames.contains(preview->remoteBranch) ||
            preview->remoteBranch.startsWith(QStringLiteral("release/")) ||
            remoteHead == QStringLiteral("%1/%2").arg(preview->remote, preview->remoteBranch);
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

} // namespace gity::session
