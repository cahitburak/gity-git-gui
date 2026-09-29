// Undo and redo for what Gity does to a repository.
//
// Before an operation that can lose something — commit, merge, rebase, reset,
// checkout, deleting a branch or tag, popping or dropping a stash, discarding
// changes — the worker captures the repository's state: where every branch
// and tag points, HEAD, the stash list, and a snapshot of the working copy
// and index as trees. After it, the new ref positions. Undo puts back what the
// operation changed; redo reverses the undo.
//
// Everything stays real git. The snapshots are ordinary tree and commit
// objects, kept from garbage collection by the reflog of one ref,
// refs/gity/snapshots — the same way `git stash` keeps its entries — so they
// last as long as git's own reflog expiry (30 days by default) and can be
// inspected with any git tool. The log itself is a small JSON file in the git
// directory.
//
// Everything here runs on the session's worker thread.
#pragma once

#include "core/git/UndoPlan.h"

#include <QDateTime>
#include <QString>

#include <vector>

namespace gity::session {

struct RepoState {
    /// "refs/heads/x" when HEAD is on a branch; empty when detached or unborn.
    QString headRef;
    QString headOid;
    git::RefMap refs; ///< refs/heads and refs/tags.
    std::vector<git::StashEntry> stashes; ///< Newest first.
    /// The working copy, untracked files included, and the index, as trees.
    /// Captured only for the state an undo returns to.
    QString worktreeTree;
    QString indexTree;
};

struct UndoEntry {
    QString verb;
    QDateTime when;
    RepoState before;
    RepoState after;
    /// The operation stopped part way — a conflicted merge — and is waiting to
    /// be continued or abandoned. It is completed, or dropped, by whatever
    /// finishes it.
    bool pending = false;
};

class UndoLog {
public:
    /// Reads the log for the repository at `workdir`, whose git directory is
    /// `gitDir`.
    void load(const QString& workdir, const QString& gitDir);

    /// The repository as it is now. `withWorktree` adds the snapshot trees;
    /// `worktreeClean` lets a clean working copy use HEAD's tree instead of
    /// hashing every file.
    [[nodiscard]] RepoState capture(bool withWorktree, bool worktreeClean) const;

    /// Records `verb`, which started from `before`. Dropped if it changed
    /// nothing. `worktreeChanged` is for operations that touch only files, such
    /// as a discard, which leave every ref as it was.
    void record(const QString& verb, RepoState before, bool worktreeChanged,
                bool operationInProgress);

    /// An operation is stopped part way and waiting to be finished.
    [[nodiscard]] bool hasPending() const;
    [[nodiscard]] QString undoLabel() const;
    [[nodiscard]] QString redoLabel() const;

    struct Outcome {
        bool ok = false;
        QString message;
    };
    Outcome undo(bool operationInProgress, bool worktreeClean);
    Outcome redo(bool operationInProgress, bool worktreeClean);

    static constexpr std::size_t kLimit = 50;

private:
    Outcome restore(std::vector<UndoEntry>& from, std::vector<UndoEntry>& to,
                    bool operationInProgress, bool worktreeClean, const QString& word);
    void keepAlive(const UndoEntry& entry) const;
    void save() const;

    QString workdir_;
    QString gitDir_;
    std::vector<UndoEntry> undo_;
    std::vector<UndoEntry> redo_;
};

} // namespace gity::session
