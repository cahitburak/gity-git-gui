// RepoWorker: operations that rewrite history — merge, rebase,
// cherry-pick and revert, the interactive rebase and its edit stops.
#include "RepoWorker.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

namespace gity::session {

namespace {

QString readTrimmed(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

} // namespace

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
    const QString editing = own.exists(QStringLiteral("edit-stop-message"))
                                ? readTrimmed(own.filePath(QStringLiteral("edit-stop-message")))
                                      .section(QChar('\n'), 0, 0)
                                : QString();
    emit editStopChanged(editing);
    emit operationChanged(
        static_cast<int>(operation),
        QString::fromUtf8(gity::git::operationNoun(operation).data(),
                          static_cast<qsizetype>(gity::git::operationNoun(operation).size())));
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
    const bool stopped =
        operation != gity::git::Operation::None && operation != gity::git::Operation::Bisect;

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
    const GitResult head =
        GitProcess::run(workdir_, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    if (!head.ok() || head.output.trimmed() != stopped) {
        return {}; // already prepared, or the user moved on
    }
    const GitResult unmerged =
        GitProcess::run(workdir_, {QStringLiteral("ls-files"), QStringLiteral("-u")});
    const GitResult parent =
        GitProcess::run(workdir_, {QStringLiteral("rev-parse"), QStringLiteral("-q"),
                                   QStringLiteral("--verify"), QStringLiteral("HEAD^")});
    if (!unmerged.output.trimmed().isEmpty() || !parent.ok()) {
        return {}; // a conflict, or a root commit with nothing to reset to
    }
    const GitResult who = GitProcess::run(
        workdir_, {QStringLiteral("show"), QStringLiteral("-s"), QStringLiteral("--date=raw"),
                   QStringLiteral("--format=%an%n%ae%n%ad"), QStringLiteral("HEAD")});
    const GitResult message =
        GitProcess::run(workdir_, {QStringLiteral("show"), QStringLiteral("-s"),
                                   QStringLiteral("--format=%B"), QStringLiteral("HEAD")});
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
        const bool staged =
            !GitProcess::run(workdir_, {QStringLiteral("diff"), QStringLiteral("--cached"),
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
        // Quoted properly: the temporary directory is under the user's
        // TMPDIR, and a quote in that path broke the command — or ran
        // whatever followed it.
        {QStringLiteral("GIT_SEQUENCE_EDITOR"),
         QStringLiteral("cp %1").arg(
             QString::fromStdString(gity::git::shellQuote(todoPath.toStdString())))},
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
    const bool stopped =
        operation != gity::git::Operation::None && operation != gity::git::Operation::Bisect;

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

void RepoWorker::loadHeadAncestors(int limit) {
    if (!repo_) {
        return;
    }
    const GitResult result = GitProcess::run(
        workdir_, {QStringLiteral("log"), QStringLiteral("--first-parent"), QStringLiteral("-n"),
                   QString::number(limit), QStringLiteral("--format=%H%x00%s"),
                   QStringLiteral("HEAD"), QStringLiteral("--")});
    QStringList oids;
    QStringList subjects;
    for (const QString& line : result.output.split(QChar('\n'), Qt::SkipEmptyParts)) {
        const qsizetype separator = line.indexOf(QChar('\0'));
        if (separator > 0) {
            oids << line.left(separator);
            subjects << line.mid(separator + 1);
        }
    }
    emit headAncestorsReady(oids, subjects);
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
        workdir_,
        {QStringLiteral("log"), QStringLiteral("--topo-order"), QStringLiteral("--no-merges"),
         QStringLiteral("--format=%H%x00%s"), QStringLiteral("%1..HEAD").arg(baseOid)});
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

} // namespace gity::session
