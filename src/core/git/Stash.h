// M2 — the stash.
//
// Untracked files are the subtlety. `git stash` leaves them behind by default,
// so the stash holds only part of the change while the rest stays in the
// worktree — a half-state that is confusing to come back to. Including
// untracked files is the sane default.
#pragma once

#include "Repository.h"

#include <cstddef>
#include <string>
#include <vector>

namespace gity::git {

struct StashEntry {
    std::size_t index = 0; ///< 0 is the most recent, matching stash@{0}.
    std::string message;
    git_oid target{};
};

[[nodiscard]] std::vector<StashEntry> listStashes(git_repository* repo);

struct StashOptions {
    /// Newly imported assets are untracked until staged, and a stash that
    /// leaves them behind is more confusing than one that takes everything.
    bool includeUntracked = true;
    /// Ignored files stay put: Library/ is tens of thousands of files and
    /// regenerating it is the build's job, not ours.
    bool includeIgnored = false;
    bool keepIndex = false;
};

/// Stashes the working copy. Returns false when there was nothing to stash,
/// which is not an error.
bool stashSave(git_repository* repo, const std::string& message, const StashOptions& options,
               git_oid* stashId = nullptr);

/// Applies a stash without removing it.
void stashApply(git_repository* repo, std::size_t index);

/// Applies a stash and drops it if the apply succeeded.
void stashPop(git_repository* repo, std::size_t index);

/// Discards a stash. Irreversible.
void stashDrop(git_repository* repo, std::size_t index);

} // namespace gity::git
