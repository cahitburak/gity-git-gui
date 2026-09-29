#include "UndoLog.h"

#include "session/GitProcess.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <set>

namespace gity::session {
namespace {

using git::RefMap;
using git::StashEntry;

/// Commits Gity makes for its own bookkeeping need an identity, and the
/// user's may not be configured; these never reach a branch.
const QList<GitProcess::EnvOverride>& internalIdentity() {
    static const QList<GitProcess::EnvOverride> identity{
        {QStringLiteral("GIT_AUTHOR_NAME"), QStringLiteral("Gity")},
        {QStringLiteral("GIT_AUTHOR_EMAIL"), QStringLiteral("gity@localhost")},
        {QStringLiteral("GIT_COMMITTER_NAME"), QStringLiteral("Gity")},
        {QStringLiteral("GIT_COMMITTER_EMAIL"), QStringLiteral("gity@localhost")},
    };
    return identity;
}

QString firstLine(const GitResult& result) {
    return result.output.section(QChar('\n'), 0, 0).trimmed();
}

bool sameRefs(const RepoState& a, const RepoState& b) {
    if (a.headRef != b.headRef || a.headOid != b.headOid || a.refs != b.refs ||
        a.stashes.size() != b.stashes.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.stashes.size(); ++i) {
        if (a.stashes[i].oid != b.stashes[i].oid) {
            return false;
        }
    }
    return true;
}

QJsonObject toJson(const RepoState& state) {
    QJsonObject refs;
    for (const auto& [name, oid] : state.refs) {
        refs.insert(QString::fromStdString(name), QString::fromStdString(oid));
    }
    QJsonArray stashes;
    for (const StashEntry& stash : state.stashes) {
        stashes.append(QJsonObject{{QStringLiteral("oid"), QString::fromStdString(stash.oid)},
                                   {QStringLiteral("message"),
                                    QString::fromStdString(stash.message)}});
    }
    return QJsonObject{{QStringLiteral("headRef"), state.headRef},
                       {QStringLiteral("headOid"), state.headOid},
                       {QStringLiteral("refs"), refs},
                       {QStringLiteral("stashes"), stashes},
                       {QStringLiteral("worktree"), state.worktreeTree},
                       {QStringLiteral("index"), state.indexTree}};
}

RepoState stateFromJson(const QJsonObject& json) {
    RepoState state;
    state.headRef = json.value(QStringLiteral("headRef")).toString();
    state.headOid = json.value(QStringLiteral("headOid")).toString();
    const QJsonObject refs = json.value(QStringLiteral("refs")).toObject();
    for (auto it = refs.begin(); it != refs.end(); ++it) {
        state.refs[it.key().toStdString()] = it.value().toString().toStdString();
    }
    for (const QJsonValue& value : json.value(QStringLiteral("stashes")).toArray()) {
        const QJsonObject stash = value.toObject();
        state.stashes.push_back(
            {stash.value(QStringLiteral("oid")).toString().toStdString(),
             stash.value(QStringLiteral("message")).toString().toStdString()});
    }
    state.worktreeTree = json.value(QStringLiteral("worktree")).toString();
    state.indexTree = json.value(QStringLiteral("index")).toString();
    return state;
}

QJsonObject toJson(const UndoEntry& entry) {
    return QJsonObject{{QStringLiteral("verb"), entry.verb},
                       {QStringLiteral("when"), entry.when.toString(Qt::ISODate)},
                       {QStringLiteral("before"), toJson(entry.before)},
                       {QStringLiteral("after"), toJson(entry.after)},
                       {QStringLiteral("pending"), entry.pending}};
}

UndoEntry entryFromJson(const QJsonObject& json) {
    UndoEntry entry;
    entry.verb = json.value(QStringLiteral("verb")).toString();
    entry.when = QDateTime::fromString(json.value(QStringLiteral("when")).toString(),
                                       Qt::ISODate);
    entry.before = stateFromJson(json.value(QStringLiteral("before")).toObject());
    entry.after = stateFromJson(json.value(QStringLiteral("after")).toObject());
    entry.pending = json.value(QStringLiteral("pending")).toBool();
    return entry;
}

} // namespace

void UndoLog::load(const QString& workdir, const QString& gitDir) {
    workdir_ = workdir;
    gitDir_ = gitDir;
    undo_.clear();
    redo_.clear();

    QFile file(QDir(gitDir_).filePath(QStringLiteral("gity/undo.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue& value : root.value(QStringLiteral("undo")).toArray()) {
        undo_.push_back(entryFromJson(value.toObject()));
    }
    for (const QJsonValue& value : root.value(QStringLiteral("redo")).toArray()) {
        redo_.push_back(entryFromJson(value.toObject()));
    }
}

void UndoLog::save() const {
    QDir().mkpath(QDir(gitDir_).filePath(QStringLiteral("gity")));
    QJsonArray undo;
    for (const UndoEntry& entry : undo_) {
        undo.append(toJson(entry));
    }
    QJsonArray redo;
    for (const UndoEntry& entry : redo_) {
        redo.append(toJson(entry));
    }
    QFile file(QDir(gitDir_).filePath(QStringLiteral("gity/undo.json")));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(QJsonObject{{QStringLiteral("undo"), undo},
                                             {QStringLiteral("redo"), redo}})
                       .toJson(QJsonDocument::Indented));
    }
}

RepoState UndoLog::capture(bool withWorktree, bool worktreeClean) const {
    const GitProcess::InternalScope internal;
    RepoState state;

    const GitResult head =
        GitProcess::run(workdir_, {QStringLiteral("symbolic-ref"), QStringLiteral("-q"),
                                   QStringLiteral("HEAD")});
    if (head.ok()) {
        state.headRef = firstLine(head);
    }
    const GitResult headOid =
        GitProcess::run(workdir_, {QStringLiteral("rev-parse"), QStringLiteral("-q"),
                                   QStringLiteral("--verify"), QStringLiteral("HEAD")});
    if (headOid.ok()) {
        state.headOid = firstLine(headOid);
    }

    const GitResult refs = GitProcess::run(
        workdir_, {QStringLiteral("for-each-ref"), QStringLiteral("--format=%(refname) %(objectname)"),
                   QStringLiteral("refs/heads"), QStringLiteral("refs/tags")});
    for (const QString& line : refs.output.split(QChar('\n'), Qt::SkipEmptyParts)) {
        const qsizetype space = line.lastIndexOf(QChar(' '));
        if (space > 0) {
            state.refs[line.left(space).toStdString()] = line.mid(space + 1).toStdString();
        }
    }

    const GitResult stashes = GitProcess::run(
        workdir_, {QStringLiteral("stash"), QStringLiteral("list"),
                   QStringLiteral("--format=%H%x00%gs")});
    for (const QString& line : stashes.output.split(QChar('\n'), Qt::SkipEmptyParts)) {
        const qsizetype separator = line.indexOf(QChar('\0'));
        if (separator > 0) {
            state.stashes.push_back(
                {line.left(separator).toStdString(), line.mid(separator + 1).toStdString()});
        }
    }

    if (!withWorktree) {
        return state;
    }

    if (worktreeClean) {
        // Nothing uncommitted: the working copy is HEAD's tree, and hashing
        // every file to learn that would be waste.
        if (!state.headOid.isEmpty()) {
            state.worktreeTree = firstLine(GitProcess::run(
                workdir_, {QStringLiteral("rev-parse"), state.headOid + QStringLiteral("^{tree}")}));
        } else {
            state.worktreeTree = firstLine(GitProcess::runWithInput(
                workdir_, {QStringLiteral("mktree")}, QByteArray()));
        }
        state.indexTree = state.worktreeTree;
        return state;
    }

    // The working copy as a tree, untracked files included, built in a
    // scratch index so the real one is not touched. Starting from a copy of
    // the real index lets git reuse its record of which files are unchanged.
    const QString scratch = QDir(gitDir_).filePath(QStringLiteral("gity/snapshot-index"));
    QDir().mkpath(QDir(gitDir_).filePath(QStringLiteral("gity")));
    QFile::remove(scratch);
    QFile::copy(QDir(gitDir_).filePath(QStringLiteral("index")), scratch);
    const QList<GitProcess::EnvOverride> scratchIndex{{QStringLiteral("GIT_INDEX_FILE"), scratch}};
    const GitResult added = GitProcess::runWithEnv(
        // Not --force: ignored files — build output, dependencies — are not
        // the user's work, and hashing them would make every snapshot slow.
        workdir_, {QStringLiteral("add"), QStringLiteral("--all"),
                   QStringLiteral("--ignore-errors"), QStringLiteral(":/")},
        scratchIndex);
    Q_UNUSED(added)
    const GitResult tree =
        GitProcess::runWithEnv(workdir_, {QStringLiteral("write-tree")}, scratchIndex);
    QFile::remove(scratch);
    if (tree.ok()) {
        state.worktreeTree = firstLine(tree);
    }
    // The index as it stands — what was staged. A conflicted index cannot
    // be written as a tree; then the working copy stands in for it.
    const GitResult index = GitProcess::run(workdir_, {QStringLiteral("write-tree")});
    state.indexTree = index.ok() ? firstLine(index) : state.worktreeTree;
    return state;
}

void UndoLog::keepAlive(const UndoEntry& entry) const {
    if (entry.before.worktreeTree.isEmpty()) {
        return;
    }
    const GitProcess::InternalScope internal;

    // Everything the undo may need to put back, made reachable from one
    // commit: the index tree, and every commit the operation moved away from
    // or removed — a deleted branch's tip is otherwise reachable from nothing.
    QStringList parents;
    const GitResult indexCommit = GitProcess::runWithEnv(
        workdir_, {QStringLiteral("commit-tree"), entry.before.indexTree, QStringLiteral("-m"),
                   QStringLiteral("gity: index before %1").arg(entry.verb)},
        internalIdentity());
    if (indexCommit.ok()) {
        parents << firstLine(indexCommit);
    }
    std::set<QString> keep;
    if (!entry.before.headOid.isEmpty()) {
        keep.insert(entry.before.headOid);
    }
    for (const auto& [name, oid] : entry.before.refs) {
        const auto after = entry.after.refs.find(name);
        if ((after == entry.after.refs.end() || after->second != oid) &&
            name.rfind("refs/heads/", 0) == 0) {
            keep.insert(QString::fromStdString(oid));
        }
    }
    for (const StashEntry& stash : entry.before.stashes) {
        keep.insert(QString::fromStdString(stash.oid));
    }
    for (const QString& oid : keep) {
        if (!parents.contains(oid)) {
            parents << oid;
        }
    }

    QStringList args{QStringLiteral("commit-tree"), entry.before.worktreeTree};
    for (const QString& parent : parents) {
        args << QStringLiteral("-p") << parent;
    }
    args << QStringLiteral("-m") << QStringLiteral("gity: before %1").arg(entry.verb);
    const GitResult snapshot = GitProcess::runWithEnv(workdir_, args, internalIdentity());
    if (!snapshot.ok()) {
        return;
    }
    static_cast<void>(GitProcess::run(
        workdir_, {QStringLiteral("update-ref"), QStringLiteral("--create-reflog"),
                   QStringLiteral("-m"), QStringLiteral("gity: before %1").arg(entry.verb),
                   QStringLiteral("refs/gity/snapshots"), firstLine(snapshot)}));
}

void UndoLog::record(const QString& verb, RepoState before, bool worktreeChanged,
                     bool operationInProgress) {
    const RepoState after = capture(false, true);

    // An operation that stopped earlier is completed — or abandoned — by
    // whatever runs next: continue, abort, or a commit that concludes a
    // merge. It stays one entry, so undoing it undoes the whole thing.
    if (!undo_.empty() && undo_.back().pending) {
        UndoEntry& open = undo_.back();
        if (operationInProgress) {
            open.after = after;
        } else if (sameRefs(open.before, after)) {
            undo_.pop_back(); // abandoned: nothing left to undo
        } else {
            open.after = after;
            open.pending = false;
            keepAlive(open);
        }
        save();
        return;
    }

    UndoEntry entry;
    entry.verb = verb;
    entry.when = QDateTime::currentDateTime();
    entry.before = std::move(before);
    entry.after = after;
    entry.pending = operationInProgress;
    if (!operationInProgress && !worktreeChanged && sameRefs(entry.before, entry.after)) {
        return; // it changed nothing
    }
    keepAlive(entry);
    undo_.push_back(std::move(entry));
    while (undo_.size() > kLimit) {
        undo_.erase(undo_.begin());
    }
    redo_.clear();
    save();
}

bool UndoLog::hasPending() const {
    return !undo_.empty() && undo_.back().pending;
}

QString UndoLog::undoLabel() const {
    return undo_.empty() || undo_.back().pending ? QString() : undo_.back().verb;
}

QString UndoLog::redoLabel() const {
    return redo_.empty() ? QString() : redo_.back().verb;
}

UndoLog::Outcome UndoLog::undo(bool operationInProgress, bool worktreeClean) {
    return restore(undo_, redo_, operationInProgress, worktreeClean, QObject::tr("Undid"));
}

UndoLog::Outcome UndoLog::redo(bool operationInProgress, bool worktreeClean) {
    return restore(redo_, undo_, operationInProgress, worktreeClean, QObject::tr("Redid"));
}

UndoLog::Outcome UndoLog::restore(std::vector<UndoEntry>& from, std::vector<UndoEntry>& to,
                                  bool operationInProgress, bool worktreeClean,
                                  const QString& word) {
    if (operationInProgress) {
        return {false, QObject::tr("Finish or abandon the operation in progress first.")};
    }
    if (from.empty() || from.back().pending) {
        return {false, QObject::tr("Nothing to undo.")};
    }
    const UndoEntry entry = from.back();
    const GitProcess::InternalScope internal;
    const QString reason = QStringLiteral("gity: %1 %2").arg(word.toLower(), entry.verb);

    // The state being left, in full: it is what the opposite action returns
    // to, so nothing the undo overwrites is lost.
    RepoState leaving = capture(true, worktreeClean);

    // Refs first, in one transaction, and only the ones still where the
    // operation left them (UndoPlan).
    const git::RefRestorePlan plan =
        git::planRefRestore(entry.before.refs, entry.after.refs, leaving.refs);
    if (!plan.changes.empty()) {
        QByteArray commands;
        for (const git::RefChange& change : plan.changes) {
            const QByteArray ref = QByteArray::fromStdString(change.ref);
            if (change.to.empty()) {
                commands += "delete " + ref + " " + QByteArray::fromStdString(change.from) + "\n";
            } else if (change.from.empty()) {
                commands += "create " + ref + " " + QByteArray::fromStdString(change.to) + "\n";
            } else {
                commands += "update " + ref + " " + QByteArray::fromStdString(change.to) + " " +
                            QByteArray::fromStdString(change.from) + "\n";
            }
        }
        const GitResult updated = GitProcess::runWithInput(
            workdir_, {QStringLiteral("update-ref"), QStringLiteral("-m"), reason,
                       QStringLiteral("--stdin")},
            commands);
        if (!updated.ok()) {
            return {false, QObject::tr("Could not move the branches back: %1")
                               .arg(updated.firstProblemLine())};
        }
    }

    // If the branch HEAD is on moved again since, the working copy belongs to
    // that newer state: rolling the files back under it would make the new
    // commit look like uncommitted changes. HEAD and files are left alone.
    const std::string headBranch = entry.after.headRef.toStdString();
    const bool headSkipped =
        !headBranch.empty() && std::find(plan.skipped.begin(), plan.skipped.end(),
                                         headBranch) != plan.skipped.end();

    // HEAD, when the operation moved it: back onto the branch it was on, or
    // back to the commit it was detached at.
    if (headSkipped) {
        // see above
    } else if (entry.before.headRef != entry.after.headRef ||
        (entry.before.headRef.isEmpty() && entry.before.headOid != entry.after.headOid)) {
        if (!entry.before.headRef.isEmpty()) {
            static_cast<void>(GitProcess::run(
                workdir_, {QStringLiteral("symbolic-ref"), QStringLiteral("-m"), reason,
                           QStringLiteral("HEAD"), entry.before.headRef}));
        } else if (!entry.before.headOid.isEmpty()) {
            static_cast<void>(GitProcess::run(
                workdir_, {QStringLiteral("update-ref"), QStringLiteral("--no-deref"),
                           QStringLiteral("-m"), reason, QStringLiteral("HEAD"),
                           entry.before.headOid}));
        }
    }

    // The working copy and the index, as they were.
    if (!headSkipped && !entry.before.worktreeTree.isEmpty()) {
        const GitResult files = GitProcess::run(
            workdir_, {QStringLiteral("read-tree"), QStringLiteral("--reset"), QStringLiteral("-u"),
                       entry.before.worktreeTree},
            300000);
        if (!files.ok()) {
            return {false, QObject::tr("Could not restore the working copy: %1")
                               .arg(files.firstProblemLine())};
        }
        static_cast<void>(GitProcess::run(
            workdir_, {QStringLiteral("read-tree"), entry.before.indexTree}));

        // Submodules are repositories of their own, so restoring this one's
        // files does not move them. The snapshot recorded where each one was
        // (a gitlink in the working-copy tree); any now elsewhere is put back,
        // detached — but only if it has no uncommitted work of its own, which
        // a checkout would otherwise have to overwrite.
        const GitResult links = GitProcess::run(
            workdir_, {QStringLiteral("ls-tree"), QStringLiteral("-r"), QStringLiteral("-z"),
                       entry.before.worktreeTree});
        for (const QString& line : links.output.split(QChar('\0'), Qt::SkipEmptyParts)) {
            // "160000 commit <oid>\t<path>"
            if (!line.startsWith(QStringLiteral("160000 commit "))) {
                continue;
            }
            const QString oid = line.section(QChar(' '), 2, 2).section(QChar('\t'), 0, 0);
            const QString path = QDir(workdir_).filePath(line.section(QChar('\t'), 1));
            if (!QDir(path).exists()) {
                continue;
            }
            const QString now = GitProcess::run(path, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")})
                                    .output.trimmed();
            const bool clean = GitProcess::run(path, {QStringLiteral("status"),
                                                      QStringLiteral("--porcelain")})
                                   .output.trimmed()
                                   .isEmpty();
            if (now != oid && clean) {
                static_cast<void>(GitProcess::run(
                    path, {QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("--detach"),
                           oid}));
            }
        }
    }

    // Stashes the operation removed come back; ones it made go.
    const git::StashRestorePlan stashes =
        git::planStashRestore(entry.before.stashes, entry.after.stashes, leaving.stashes);
    for (const std::string& oid : stashes.drop) {
        const GitResult list = GitProcess::run(
            workdir_, {QStringLiteral("stash"), QStringLiteral("list"), QStringLiteral("--format=%H")});
        const QStringList oids = list.output.split(QChar('\n'), Qt::SkipEmptyParts);
        const qsizetype index = oids.indexOf(QString::fromStdString(oid));
        if (index >= 0) {
            static_cast<void>(GitProcess::run(
                workdir_, {QStringLiteral("stash"), QStringLiteral("drop"), QStringLiteral("-q"),
                           QStringLiteral("stash@{%1}").arg(index)}));
        }
    }
    for (const StashEntry& stash : stashes.store) {
        static_cast<void>(GitProcess::run(
            workdir_, {QStringLiteral("stash"), QStringLiteral("store"), QStringLiteral("-m"),
                       QString::fromStdString(stash.message), QString::fromStdString(stash.oid)}));
    }

    UndoEntry opposite;
    opposite.verb = entry.verb;
    opposite.when = QDateTime::currentDateTime();
    opposite.before = std::move(leaving);
    opposite.after = capture(false, true);
    keepAlive(opposite);

    from.pop_back();
    to.push_back(std::move(opposite));
    save();

    QString message = QObject::tr("%1 %2.").arg(word, entry.verb);
    if (!plan.skipped.empty()) {
        QStringList names;
        for (const std::string& name : plan.skipped) {
            names << QString::fromStdString(name).section(QChar('/'), 2);
        }
        message += QObject::tr(" Left alone because they changed since: %1.")
                       .arg(names.join(QStringLiteral(", ")));
        if (headSkipped) {
            message += QObject::tr(" The working copy was left as it is.");
        }
    }
    return {true, message};
}

} // namespace gity::session
