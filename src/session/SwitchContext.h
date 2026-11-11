// What switching the working copy to a ref involves, read from the refs and
// status already loaded — what Checkout and New Branch ask about before
// anything moves.
#pragma once

#include "SessionTypes.h"

#include <QString>

namespace gity::session {

struct SwitchContext {
    /// A local branch, a remote one, or anything else, checked out detached.
    RefKind kind = RefKind::Detached;
    bool hasSubmodules = false;
    /// How many of the uncommitted changes are only submodules at another
    /// commit — not edits anyone made, and exactly what switching branches
    /// leaves behind when submodules are not updated.
    int submoduleChanges = 0;
    /// The ref is at the commit already checked out, or is empty (HEAD):
    /// switching moves nothing on disk, so there is nothing to decide about
    /// the changes.
    bool sameCommit = false;
};

/// `ref` by shorthand: "main", "origin/main", a tag or a commit id. Either
/// pointer may be null while it has not loaded.
[[nodiscard]] SwitchContext switchContext(const gity::git::RefSet* refs,
                                          const gity::git::WorkingCopyStatus* status,
                                          const QString& ref);

} // namespace gity::session
