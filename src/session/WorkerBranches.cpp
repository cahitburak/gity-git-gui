// RepoWorker: switching, creating and deleting branches, tags, resets and
// stashes — the local commands that move refs.
#include "RepoWorker.h"

#include "core/git/Submodules.h"

namespace gity::session {

void RepoWorker::runCheckout(const QString& ref, int kindValue, int changesValue,
                             bool updateSubmodules) {
    using Kind = RefKind;
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
    using Kind = RefKind;
    const auto kind = static_cast<Kind>(kindValue);
    // Started from someone else's branch under a new name, it is a new line of
    // work, not a copy of theirs: tracking origin/develop would make Pull
    // bring develop in and Push refuse the mismatched name. Push publishes it
    // under its own name instead. Named the same, tracking is the point.
    const bool noTrack = kind == Kind::Remote && startPoint.section(QChar('/'), 1) != name;
    QStringList args;
    if (checkout) {
        // The name is the value of -c, so git never reads it as an option.
        args << QStringLiteral("switch") << QStringLiteral("-c") << name;
        if (noTrack) {
            args << QStringLiteral("--no-track");
        }
    } else {
        // `--` before the name: typed into a dialog, "-D" with a start point
        // of "main" was `git branch -D main`, which deletes main. After it,
        // git refuses the name as one no branch can have.
        args << QStringLiteral("branch");
        if (noTrack) {
            args << QStringLiteral("--no-track");
        }
        args << QStringLiteral("--") << name;
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
    return GitProcess::run(workdir_,
                           {QStringLiteral("show-ref"), QStringLiteral("--verify"),
                            QStringLiteral("--quiet"), QStringLiteral("refs/heads/%1").arg(name)})
        .ok();
}

void RepoWorker::runSwitch(const QString& verb, QStringList args, const QString& target,
                           int changesValue, bool updateSubmodules) {
    using Changes = LocalChanges;
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
            workdir_,
            {QStringLiteral("stash"), QStringLiteral("push"), QStringLiteral("--include-untracked"),
             QStringLiteral("--message"), QObject::tr("Gity: before switching to %1").arg(target)});
        if (!stash.ok()) {
            finish(false,
                   QObject::tr("Could not stash the changes: %1").arg(stash.firstProblemLine()));
            return;
        }
        stashed = !stash.output.contains(QStringLiteral("No local changes"));
    }

    const GitResult switched = GitProcess::run(workdir_, args, 300000);
    if (!switched.ok()) {
        // Nothing switched: the stash goes straight back where it came from.
        if (stashed) {
            static_cast<void>(
                GitProcess::run(workdir_, {QStringLiteral("stash"), QStringLiteral("pop")}));
        }
        finish(false, switched.firstProblemLine());
        return;
    }

    QStringList notes;
    if (updateSubmodules) {
        const GitResult submodules =
            GitProcess::run(workdir_,
                            {QStringLiteral("submodule"), QStringLiteral("update"),
                             QStringLiteral("--init"), QStringLiteral("--recursive")},
                            600000);
        notes << (submodules.ok() ? QObject::tr("submodules moved to the commits it records")
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
    finish(true,
           notes.isEmpty()
               ? QObject::tr("Switched to %1.").arg(target)
               : QObject::tr("Switched to %1: %2.").arg(target, notes.join(QStringLiteral("; "))));
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
        const std::vector<std::string> lines =
            gity::git::splitGitOutput(result.output.isEmpty() ? result.errorOutput.toStdString()
                                                              : result.output.toStdString());
        emit historyEditFinished(verb, true, false,
                                 lines.empty() ? QString() : QString::fromStdString(lines.front()));
    } else {
        emit historyEditFinished(verb, false, false, result.firstProblemLine());
    }

    reportOperation();
    refreshStatus();
}

} // namespace gity::session
