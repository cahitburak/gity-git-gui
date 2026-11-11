// Every change to the index or to a file's content in the worktree, through
// the git binary (ADR-003).
//
// These used to be libgit2 calls, and that was a real defect, not a matter of
// style: libgit2 runs its built-in filters (CRLF, ident) but never an external
// `filter.<name>.clean`. Staging a file tracked by LFS put the whole binary in
// the index instead of its pointer, and staging one under git-crypt committed
// the plaintext. `git add` and `git apply` run the user's filters exactly as
// their terminal does.
#pragma once

#include "GitProcess.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace gity::session::index {

/// `git add -A` for exactly these paths: content, deletions and new files
/// alike, through the clean filters.
[[nodiscard]] GitResult stagePaths(const QString& workdir, const QStringList& paths);

/// `git reset` for exactly these paths: each goes back to its HEAD version,
/// or out of the index when HEAD has none, and a conflict is resolved to HEAD.
[[nodiscard]] GitResult unstagePaths(const QString& workdir, const QStringList& paths);

/// Applies a patch to the index alone (`git apply --cached`), leaving the
/// worktree as it is. The patch is bytes, not text: a file in Latin-1 or with
/// CR LF endings must reach git exactly as it was read.
[[nodiscard]] GitResult applyToIndex(const QString& workdir, const QByteArray& patch);

/// Applies a patch to the worktree alone (`git apply`) — what discarding an
/// unstaged hunk means.
[[nodiscard]] GitResult applyToWorktree(const QString& workdir, const QByteArray& patch);

} // namespace gity::session::index
