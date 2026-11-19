// The values a repository session hands the UI: immutable, shared, and safe
// to read from any thread once delivered (ADR-004). Kept apart from
// RepoSession.h so a panel that only shows them need not know the session.
#pragma once

#include "ImageDiff.h"
#include "core/git/Blame.h"
#include "core/git/BlobReader.h"
#include "core/git/CommitDetail.h"
#include "core/git/Compare.h"
#include "core/git/Conflict.h"
#include "core/git/FileDiff.h"
#include "core/git/HistoryStream.h"
#include "core/git/HunkStaging.h"
#include "core/git/LfsLocks.h"
#include "core/git/OperationState.h"
#include "core/git/RebaseTodo.h"
#include "core/git/Refs.h"
#include "core/git/Remotes.h"
#include "core/git/Staging.h"
#include "core/git/WorkingCopy.h"
#include "core/model/HistoryChunk.h"

#include <QMetaType>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace gity::session {

using ChunkPtr = std::shared_ptr<const model::HistoryChunk>;
using RefSetPtr = std::shared_ptr<const git::RefSet>;
using DetailPtr = std::shared_ptr<const git::CommitDetail>;
using FileDiffPtr = std::shared_ptr<const git::FileDiff>;
using StatusPtr = std::shared_ptr<const git::WorkingCopyStatus>;
using RemoteListPtr = std::shared_ptr<const std::vector<git::RemoteEntry>>;
using HunkStagingPtr = std::shared_ptr<const std::vector<git::HunkStaging>>;
using LockListPtr = std::shared_ptr<const std::vector<git::LfsLock>>;
using ComparisonPtr = std::shared_ptr<const git::Comparison>;
using BlamePtr = std::shared_ptr<const git::Blame>;
using ConflictPtr = std::shared_ptr<const git::ConflictFile>;

/// What an operation would do, worked out before it runs.
struct Preview {
    enum class Kind { Pull, Merge, Rebase, Push };
    Kind kind = Kind::Pull;
    QString branch; ///< The local branch acted on.
    QString target; ///< Its upstream, or the branch merged or rebased onto.
    /// Non-empty when no preview could be made; says why.
    QString problem;

    /// Commits `target` has that `branch` lacks, and the reverse, with up to
    /// ten of each described.
    int incoming = 0;
    int outgoing = 0;
    QStringList incomingCommits;
    QStringList outgoingCommits;
    bool upToDate = false;
    bool fastForward = false;

    /// From a trial merge in memory. `predicted` false when this git cannot
    /// make one; `predictionNote` then says so.
    bool predicted = false;
    QStringList conflicts;
    QString predictionNote;

    /// Uncommitted files that would make git refuse.
    QStringList blockedByLocal;

    // Push only.
    bool newBranch = false;
    /// The push makes the branch track what it creates. Not so for a branch
    /// that already tracks the upstream and is first pushed to a fork.
    bool setsUpstream = false;
    QString remote;
    QString remoteBranch;
    /// The remote branch's tip as last fetched: what a force push leases
    /// against, so it fails rather than overwrite work pushed since.
    QString leaseOid;
    /// A branch other people are likely to build on: main, master, develop,
    /// trunk, release/*, or the remote's default.
    bool shared = false;
};

using PreviewPtr = std::shared_ptr<const Preview>;

struct RepoInfo {
    QString workdir;
    QString headRef;
};

/// What a checkout does with uncommitted changes.
enum class LocalChanges { Keep, StashAndReapply, Discard };
/// What a ref being switched to is: a local branch, a remote branch (a local
/// one tracking it is created, or switched to if it exists), or anything
/// else — a tag or a commit — which is checked out detached.
enum class RefKind { Local, Remote, Detached };

} // namespace gity::session

Q_DECLARE_METATYPE(gity::session::RepoInfo)
Q_DECLARE_METATYPE(gity::session::ChunkPtr)
Q_DECLARE_METATYPE(gity::session::RefSetPtr)
Q_DECLARE_METATYPE(gity::session::DetailPtr)
Q_DECLARE_METATYPE(gity::session::ComparisonPtr)
Q_DECLARE_METATYPE(gity::session::PreviewPtr)
Q_DECLARE_METATYPE(gity::session::FileDiffPtr)
Q_DECLARE_METATYPE(gity::session::StatusPtr)
Q_DECLARE_METATYPE(gity::session::BlamePtr)
Q_DECLARE_METATYPE(gity::session::ConflictPtr)
