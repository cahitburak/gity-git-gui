#include "UndoLog.h"

#include "session/GitProcess.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QUuid>

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
        stashes.append(
            QJsonObject{{QStringLiteral("oid"), QString::fromStdString(stash.oid)},
                        {QStringLiteral("message"), QString::fromStdString(stash.message)}});
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
        state.stashes.push_back({stash.value(QStringLiteral("oid")).toString().toStdString(),
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
    entry.when = QDateTime::fromString(json.value(QStringLiteral("when")).toString(), Qt::ISODate);
    entry.before = stateFromJson(json.value(QStringLiteral("before")).toObject());
    entry.after = stateFromJson(json.value(QStringLiteral("after")).toObject());
    entry.pending = json.value(QStringLiteral("pending")).toBool();
    return entry;
}

} // namespace

namespace {

/// Held while the log is read, changed and written back. Two windows on one
/// repository each keep a log in memory; without this, whichever saved last
/// erased what the other had recorded.
class LogLock {
public:
    explicit LogLock(const QString& gitDir)
        : lock_(QDir(gitDir).filePath(QStringLiteral("gity/undo.lock"))) {
        QDir().mkpath(QDir(gitDir).filePath(QStringLiteral("gity")));
        // A lock left by a crashed Gity is stale after this long; nothing
        // legitimately holds it for more than a restore takes.
        lock_.setStaleLockTime(10 * 60 * 1000);
        locked_ = lock_.tryLock(10000);
        if (!locked_) {
            qWarning("gity: the undo log is locked by another process; going ahead without it");
        }
    }

    /// Recording goes ahead without the lock — an entry lost to a race is
    /// better than one never written — but a restore does not: two at once
    /// would both move the files.
    [[nodiscard]] bool locked() const { return locked_; }

private:
    bool locked_ = false;
    QLockFile lock_;
};

} // namespace

QString UndoLog::logPath() const {
    return QDir(gitDir_).filePath(QStringLiteral("gity/undo.json"));
}

void UndoLog::load(const QString& workdir, const QString& gitDir) {
    workdir_ = workdir;
    gitDir_ = gitDir;
    const LogLock lock(gitDir_);
    read();
}

void UndoLog::read() {
    undo_.clear();
    redo_.clear();

    QFile file(logPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        // Not silently empty: set aside where it can be looked at, and said.
        const QString aside =
            logPath() + QStringLiteral(".corrupt-") +
            QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmss"));
        QFile::rename(logPath(), aside);
        qWarning("gity: the undo log could not be read (%s); it was moved to %s",
                 qPrintable(error.errorString()), qPrintable(aside));
        return;
    }
    const QJsonObject root = document.object();
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
    // Written beside and renamed into place: a crash part way through leaves
    // the previous log, never half of one.
    QSaveFile file(logPath());
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("gity: could not write the undo log: %s", qPrintable(file.errorString()));
        return;
    }
    file.write(
        QJsonDocument(QJsonObject{{QStringLiteral("undo"), undo}, {QStringLiteral("redo"), redo}})
            .toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qWarning("gity: could not write the undo log: %s", qPrintable(file.errorString()));
    }
}

RepoState UndoLog::capture(bool withWorktree, bool worktreeClean) const {
    const GitProcess::InternalScope internal;
    RepoState state;

    const GitResult head = GitProcess::run(
        workdir_, {QStringLiteral("symbolic-ref"), QStringLiteral("-q"), QStringLiteral("HEAD")});
    // symbolic-ref and rev-parse fail legitimately (detached, unborn); what
    // makes a capture worthless is git not finishing at all.
    state.valid = head.started && !head.timedOut;
    if (head.ok()) {
        state.headRef = firstLine(head);
    }
    const GitResult headOid =
        GitProcess::run(workdir_, {QStringLiteral("rev-parse"), QStringLiteral("-q"),
                                   QStringLiteral("--verify"), QStringLiteral("HEAD")});
    if (headOid.ok()) {
        state.headOid = firstLine(headOid);
    }
    state.valid = state.valid && headOid.started && !headOid.timedOut;

    const GitResult refs =
        GitProcess::run(workdir_, {QStringLiteral("for-each-ref"),
                                   QStringLiteral("--format=%(refname) %(objectname)"),
                                   QStringLiteral("refs/heads"), QStringLiteral("refs/tags")});
    state.valid = state.valid && refs.ok();
    for (const QString& line : refs.output.split(QChar('\n'), Qt::SkipEmptyParts)) {
        const qsizetype space = line.lastIndexOf(QChar(' '));
        if (space > 0) {
            state.refs[line.left(space).toStdString()] = line.mid(space + 1).toStdString();
        }
    }

    const GitResult stashes =
        GitProcess::run(workdir_, {QStringLiteral("stash"), QStringLiteral("list"),
                                   QStringLiteral("--format=%H%x00%gs")});
    state.valid = state.valid && stashes.ok();
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
        // Checked: rev-parse echoes an argument it cannot resolve, and that
        // echo is not a tree anything can be restored to.
        const GitResult tree =
            !state.headOid.isEmpty()
                ? GitProcess::run(workdir_,
                                  {QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                                   state.headOid + QStringLiteral("^{tree}")})
                : GitProcess::runWithInput(workdir_, {QStringLiteral("mktree")}, QByteArray());
        if (tree.ok()) {
            state.worktreeTree = firstLine(tree);
        } else {
            state.valid = false;
        }
        state.indexTree = state.worktreeTree;
        return state;
    }

    // The working copy as a tree, untracked files included, built in a
    // scratch index so the real one is not touched. Starting from a copy of
    // the real index lets git reuse its record of which files are unchanged.
    //
    // Named per capture: two windows on one repository used to share one
    // scratch file and could build each other's snapshot.
    const QString scratch = QDir(gitDir_).filePath(QStringLiteral("gity/snapshot-index-") +
                                                   QUuid::createUuid().toString(QUuid::Id128));
    QDir().mkpath(QDir(gitDir_).filePath(QStringLiteral("gity")));
    QFile::remove(scratch);
    QFile::copy(QDir(gitDir_).filePath(QStringLiteral("index")), scratch);
    const QList<GitProcess::EnvOverride> scratchIndex{{QStringLiteral("GIT_INDEX_FILE"), scratch}};
    const GitResult added = GitProcess::runWithEnv(
        // Not --force: ignored files — build output, dependencies — are not
        // the user's work, and hashing them would make every snapshot slow.
        workdir_,
        {QStringLiteral("add"), QStringLiteral("--all"), QStringLiteral("--ignore-errors"),
         QStringLiteral(":/")},
        scratchIndex);
    Q_UNUSED(added)
    const GitResult tree =
        GitProcess::runWithEnv(workdir_, {QStringLiteral("write-tree")}, scratchIndex);
    QFile::remove(scratch);
    if (tree.ok()) {
        state.worktreeTree = firstLine(tree);
    } else {
        state.valid = false; // a "before" with no files cannot be returned to
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
        workdir_,
        {QStringLiteral("commit-tree"), entry.before.indexTree, QStringLiteral("-m"),
         QStringLiteral("gity: index before %1").arg(entry.verb)},
        internalIdentity());
    if (indexCommit.ok()) {
        parents << firstLine(indexCommit);
    }
    std::set<QString> keep;
    if (!entry.before.headOid.isEmpty()) {
        keep.insert(entry.before.headOid);
    }
    QStringList tagObjects;
    for (const auto& [name, oid] : entry.before.refs) {
        const auto after = entry.after.refs.find(name);
        if (after != entry.after.refs.end() && after->second == oid) {
            continue;
        }
        if (name.rfind("refs/heads/", 0) == 0) {
            keep.insert(QString::fromStdString(oid));
        } else if (name.rfind("refs/tags/", 0) == 0) {
            // A tag can name an annotated tag object rather than a commit, and
            // commit-tree only takes commits as parents. The commit it points
            // at is kept that way; the tag object itself below.
            const GitResult peeled = GitProcess::run(
                workdir_,
                {QStringLiteral("rev-parse"), QStringLiteral("-q"), QStringLiteral("--verify"),
                 QString::fromStdString(oid) + QStringLiteral("^{commit}")});
            if (peeled.ok()) {
                keep.insert(firstLine(peeled));
            }
            if (peeled.ok() && firstLine(peeled) != QString::fromStdString(oid)) {
                tagObjects << QString::fromStdString(oid);
            }
        }
    }
    // Each tag object an operation removed is kept by an entry in a reflog of
    // its own, exactly as the snapshots are: otherwise "Undo Delete tag"
    // fails once gc has pruned the unreachable tag.
    for (const QString& tag : tagObjects) {
        static_cast<void>(GitProcess::run(
            workdir_, {QStringLiteral("update-ref"), QStringLiteral("--create-reflog"),
                       QStringLiteral("-m"), QStringLiteral("gity: before %1").arg(entry.verb),
                       QStringLiteral("refs/gity/tags"), tag}));
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
    // A capture git did not finish — timed out, or could not start — is not
    // a state anything can be restored to, and an entry built from it would
    // undo to nonsense. Better no entry than a wrong one.
    if (!before.valid) {
        return;
    }
    const RepoState after = capture(false, true);
    if (!after.valid) {
        return;
    }

    const LogLock lock(gitDir_);
    read();

    // An operation that stopped earlier is completed — or abandoned — by
    // whatever runs next: continue, abort, or a commit that concludes a
    // merge. It stays one entry, so undoing it undoes the whole thing.
    //
    // But only if this operation began while that one was still underway. If
    // nothing was in progress when it started, the stopped operation was
    // finished elsewhere — in a terminal — and this is something new: folding
    // it in would make undoing the merge also undo an unrelated commit.
    if (!undo_.empty() && undo_.back().pending) {
        if (before.duringOperation) {
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
        closePending(before);
    }

    UndoEntry entry;
    entry.verb = verb;
    entry.when = QDateTime::currentDateTime();
    entry.before = std::move(before);
    entry.after = after;
    entry.pending = operationInProgress;
    if (!operationInProgress && !worktreeChanged && sameRefs(entry.before, entry.after)) {
        save(); // it changed nothing — but closing a stale entry may have
        return;
    }
    keepAlive(entry);
    undo_.push_back(std::move(entry));
    while (undo_.size() > kLimit) {
        undo_.erase(undo_.begin());
    }
    redo_.clear();
    save();
}

void UndoLog::closePending(const RepoState& now) {
    if (undo_.empty() || !undo_.back().pending) {
        return;
    }
    UndoEntry& open = undo_.back();
    if (sameRefs(open.before, now)) {
        undo_.pop_back(); // abandoned elsewhere: nothing of it is left
        return;
    }
    open.after = now;
    open.after.worktreeTree.clear();
    open.after.indexTree.clear();
    open.pending = false;
    keepAlive(open);
}

void UndoLog::settle(bool operationInProgress) {
    if (operationInProgress) {
        return;
    }
    const LogLock lock(gitDir_);
    read();
    if (undo_.empty() || !undo_.back().pending) {
        return;
    }
    const RepoState now = capture(false, true);
    if (!now.valid) {
        return;
    }
    closePending(now);
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

namespace {

/// The update-ref transaction that makes `changes`, and the one that takes
/// them back. Each line names the value it expects, so a ref someone moved in
/// the meantime fails the whole transaction instead of being overwritten.
QByteArray refTransaction(const std::vector<git::RefChange>& changes, bool inverse) {
    QByteArray commands;
    for (const git::RefChange& change : changes) {
        const QByteArray ref = QByteArray::fromStdString(change.ref);
        const QByteArray from = QByteArray::fromStdString(inverse ? change.to : change.from);
        const QByteArray to = QByteArray::fromStdString(inverse ? change.from : change.to);
        if (to.isEmpty()) {
            commands += "delete " + ref + " " + from + "\n";
        } else if (from.isEmpty()) {
            commands += "create " + ref + " " + to + "\n";
        } else {
            commands += "update " + ref + " " + to + " " + from + "\n";
        }
    }
    return commands;
}

} // namespace

UndoLog::Outcome UndoLog::restore(std::vector<UndoEntry>& from, std::vector<UndoEntry>& to,
                                  bool operationInProgress, bool worktreeClean,
                                  const QString& word) {
    if (operationInProgress) {
        return {false, QObject::tr("Finish or abandon the operation in progress first.")};
    }
    const LogLock lock(gitDir_);
    if (!lock.locked()) {
        return {false, QObject::tr("Another Gity window is changing this repository's undo "
                                   "history. Nothing was changed; try again in a moment.")};
    }
    read();
    if (from.empty() || from.back().pending) {
        return {false, QObject::tr("Nothing to undo.")};
    }
    const UndoEntry entry = from.back();
    const GitProcess::InternalScope internal;
    const QString reason = QStringLiteral("gity: %1 %2").arg(word.toLower(), entry.verb);

    // Everything that could stop the restore part way is checked before
    // anything moves. A half-done undo — branches back, files not — is worse
    // than one that refuses.
    if (QFile::exists(QDir(gitDir_).filePath(QStringLiteral("index.lock")))) {
        return {false, QObject::tr("Another git process is using this repository (index.lock "
                                   "exists). Nothing was changed; try again when it finishes.")};
    }

    // The state being left, in full: it is what the opposite action returns
    // to, so nothing the undo overwrites is lost. Without it there would be
    // nothing to redo back to, so no snapshot means no undo.
    RepoState leaving = capture(true, worktreeClean);
    if (!leaving.valid || leaving.worktreeTree.isEmpty()) {
        return {false, QObject::tr("Could not snapshot the working copy as it is now, so "
                                   "undoing could lose changes. Nothing was changed.")};
    }

    const git::RefRestorePlan plan =
        git::planRefRestore(entry.before.refs, entry.after.refs, leaving.refs);
    const git::StashRestorePlan stashes =
        git::planStashRestore(entry.before.stashes, entry.after.stashes, leaving.stashes);

    // Every object the restore will point at must still exist. Snapshots live
    // as long as git's reflog expiry, and a gc after that prunes them.
    {
        QByteArray wanted;
        const auto want = [&wanted](const std::string& oid) {
            if (!oid.empty()) {
                wanted += QByteArray::fromStdString(oid) + "\n";
            }
        };
        want(entry.before.worktreeTree.toStdString());
        want(entry.before.indexTree.toStdString());
        if (entry.before.headRef.isEmpty()) {
            want(entry.before.headOid.toStdString());
        }
        for (const git::RefChange& change : plan.changes) {
            want(change.to);
        }
        for (const StashEntry& stash : stashes.store) {
            want(stash.oid);
        }
        if (!wanted.isEmpty()) {
            const GitResult check = GitProcess::runWithInput(
                workdir_, {QStringLiteral("cat-file"), QStringLiteral("--batch-check")}, wanted);
            if (!check.ok() || check.output.contains(QStringLiteral(" missing"))) {
                return {false, QObject::tr("Part of what this would restore no longer exists — "
                                           "git has pruned it. Nothing was changed.")};
            }
        }
    }

    // Refs first, in one transaction, and only the ones still where the
    // operation left them (UndoPlan).
    if (!plan.changes.empty()) {
        const GitResult updated = GitProcess::runWithInput(
            workdir_,
            {QStringLiteral("update-ref"), QStringLiteral("-m"), reason, QStringLiteral("--stdin")},
            refTransaction(plan.changes, false));
        if (!updated.ok()) {
            return {false, QObject::tr("Could not move the branches back: %1. Nothing was "
                                       "changed.")
                               .arg(updated.firstProblemLine())};
        }
    }

    // Puts the refs and HEAD back as they were before this restore began, if
    // the files cannot follow them.
    const auto rollBack = [&] {
        if (!plan.changes.empty()) {
            static_cast<void>(
                GitProcess::runWithInput(workdir_,
                                         {QStringLiteral("update-ref"), QStringLiteral("-m"),
                                          reason, QStringLiteral("--stdin")},
                                         refTransaction(plan.changes, true)));
        }
        if (!leaving.headRef.isEmpty()) {
            static_cast<void>(GitProcess::run(workdir_, {QStringLiteral("symbolic-ref"),
                                                         QStringLiteral("HEAD"), leaving.headRef}));
        } else if (!leaving.headOid.isEmpty()) {
            static_cast<void>(GitProcess::run(workdir_, {QStringLiteral("update-ref"),
                                                         QStringLiteral("--no-deref"),
                                                         QStringLiteral("HEAD"), leaving.headOid}));
        }
    };
    QStringList problems;

    // If the branch HEAD is on moved again since, the working copy belongs to
    // that newer state: rolling the files back under it would make the new
    // commit look like uncommitted changes. HEAD and files are left alone.
    const std::string headBranch = entry.after.headRef.toStdString();
    const bool headBranchSkipped =
        !headBranch.empty() &&
        std::find(plan.skipped.begin(), plan.skipped.end(), headBranch) != plan.skipped.end();
    // The same goes for HEAD itself having moved: switched to another branch,
    // detached somewhere else, or a commit made on the same branch, in a
    // terminal. The files are that newer state's now, and the operation's own
    // tree has nothing to say about them; the refs it moved are still put
    // back. The commit has to be looked for here too: an operation that did
    // not move the branch — a discard, a stash, a tag — leaves it out of the
    // plan, so the check above never sees it move.
    const bool headMoved =
        leaving.headRef != entry.after.headRef || leaving.headOid != entry.after.headOid;
    const bool headSkipped = headBranchSkipped || headMoved;

    // HEAD, when the operation moved it: back onto the branch it was on, or
    // back to the commit it was detached at.
    if (headSkipped) {
        // see above
    } else if (entry.before.headRef != entry.after.headRef ||
               (entry.before.headRef.isEmpty() && entry.before.headOid != entry.after.headOid)) {
        GitResult moved;
        bool attempted = false;
        if (!entry.before.headRef.isEmpty()) {
            attempted = true;
            moved =
                GitProcess::run(workdir_, {QStringLiteral("symbolic-ref"), QStringLiteral("-m"),
                                           reason, QStringLiteral("HEAD"), entry.before.headRef});
        } else if (!entry.before.headOid.isEmpty()) {
            attempted = true;
            moved =
                GitProcess::run(workdir_, {QStringLiteral("update-ref"),
                                           QStringLiteral("--no-deref"), QStringLiteral("-m"),
                                           reason, QStringLiteral("HEAD"), entry.before.headOid});
        }
        // A git that could not even start counts as a failure too: carrying
        // on would restore the files under a HEAD still pointing elsewhere.
        if (attempted && !moved.ok()) {
            rollBack();
            return {false, QObject::tr("Could not move HEAD back: %1. Nothing was changed.")
                               .arg(moved.firstProblemLine())};
        }
    }

    // The working copy and the index, as they were.
    if (!headSkipped && !entry.before.worktreeTree.isEmpty()) {
        const GitResult files =
            GitProcess::run(workdir_,
                            {QStringLiteral("read-tree"), QStringLiteral("--reset"),
                             QStringLiteral("-u"), entry.before.worktreeTree},
                            300000);
        if (!files.ok()) {
            // read-tree -u checks before it writes, so a failure leaves the
            // files as they were; the refs are put back to match them.
            rollBack();
            return {false, QObject::tr("Could not restore the working copy: %1. Nothing was "
                                       "changed.")
                               .arg(files.firstProblemLine())};
        }
        if (!GitProcess::run(workdir_, {QStringLiteral("read-tree"), entry.before.indexTree})
                 .ok()) {
            problems << QObject::tr("what was staged could not be restored, so the index "
                                    "matches the files");
        }

        // Submodules are repositories of their own, so restoring this one's
        // files does not move them. The snapshot recorded where each one was
        // (a gitlink in the working-copy tree); any now elsewhere is put back,
        // detached — but only if it has no uncommitted work of its own, which
        // a checkout would otherwise have to overwrite.
        const GitResult links =
            GitProcess::run(workdir_, {QStringLiteral("ls-tree"), QStringLiteral("-r"),
                                       QStringLiteral("-z"), entry.before.worktreeTree});
        const QStringList entries =
            links.ok() ? links.output.split(QChar('\0'), Qt::SkipEmptyParts) : QStringList();
        for (const QString& line : entries) {
            // "160000 commit <oid>\t<path>"
            if (!line.startsWith(QStringLiteral("160000 commit "))) {
                continue;
            }
            const QString oid = line.section(QChar(' '), 2, 2).section(QChar('\t'), 0, 0);
            const QString path = QDir(workdir_).filePath(line.section(QChar('\t'), 1));
            // Only a submodule with a repository of its own: an uninitialised
            // one is an empty directory, and git run there finds the parent
            // repository instead — and would check *that* out at this commit.
            if (!QFileInfo::exists(QDir(path).filePath(QStringLiteral(".git")))) {
                continue;
            }
            const GitResult head =
                GitProcess::run(path, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
            const GitResult status =
                GitProcess::run(path, {QStringLiteral("status"), QStringLiteral("--porcelain")});
            // Not knowing is not the same as clean: a status that failed says
            // nothing about whether a checkout would overwrite work.
            if (!head.ok() || !status.ok()) {
                problems << QObject::tr("the submodule %1 could not be read, so it was left "
                                        "where it is")
                                .arg(line.section(QChar('\t'), 1));
                continue;
            }
            const QString now = head.output.trimmed();
            const bool clean = status.output.trimmed().isEmpty();
            if (now != oid && clean) {
                static_cast<void>(
                    GitProcess::run(path, {QStringLiteral("checkout"), QStringLiteral("-q"),
                                           QStringLiteral("--detach"), oid}));
            }
        }
    }

    // Stashes the operation removed come back; ones it made go.
    for (const std::string& oid : stashes.drop) {
        const GitResult list =
            GitProcess::run(workdir_, {QStringLiteral("stash"), QStringLiteral("list"),
                                       QStringLiteral("--format=%H")});
        const QStringList oids = list.output.split(QChar('\n'), Qt::SkipEmptyParts);
        const qsizetype index = oids.indexOf(QString::fromStdString(oid));
        if (index >= 0) {
            static_cast<void>(GitProcess::run(
                workdir_, {QStringLiteral("stash"), QStringLiteral("drop"), QStringLiteral("-q"),
                           QStringLiteral("stash@{%1}").arg(index)}));
        }
    }
    for (const StashEntry& stash : stashes.store) {
        if (!GitProcess::run(workdir_, {QStringLiteral("stash"), QStringLiteral("store"),
                                        QStringLiteral("-m"), QString::fromStdString(stash.message),
                                        QString::fromStdString(stash.oid)})
                 .ok()) {
            problems << QObject::tr("the stash \"%1\" could not be put back (it is still "
                                    "%2)")
                            .arg(QString::fromStdString(stash.message),
                                 QString::fromStdString(stash.oid).left(10));
        }
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
    }
    if (headMoved) {
        message += QObject::tr(" HEAD has moved since, so the working copy was left as it is.");
    } else if (headSkipped) {
        message += QObject::tr(" The working copy was left as it is.");
    }
    if (!problems.isEmpty()) {
        message += QObject::tr(" But %1.").arg(problems.join(QStringLiteral("; ")));
    }
    return {true, message};
}

} // namespace gity::session
