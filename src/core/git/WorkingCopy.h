// The working copy, split the way staging needs it.
//
// Two independent axes per path: index-vs-HEAD (staged) and
// worktree-vs-index (unstaged). A file can be both — half staged — and a
// staging UI that collapses those into one state cannot express what the user
// is looking at.
#pragma once

#include "Repository.h"

#include <string>
#include <vector>

namespace gity::git {

enum class FileState {
    Unchanged,
    Added,
    Modified,
    Deleted,
    Renamed,
    TypeChanged,
    Untracked,
    Ignored,
};

[[nodiscard]] char stateLetter(FileState state) noexcept;

struct StatusEntry {
    std::string path;
    std::string oldPath; ///< Rename source, when renamed.

    FileState staged = FileState::Unchanged;   ///< index vs HEAD
    FileState unstaged = FileState::Unchanged; ///< worktree vs index
    bool conflicted = false;

    [[nodiscard]] bool hasStaged() const noexcept { return staged != FileState::Unchanged; }
    [[nodiscard]] bool hasUnstaged() const noexcept { return unstaged != FileState::Unchanged; }
    [[nodiscard]] bool isUntracked() const noexcept { return unstaged == FileState::Untracked; }
};

struct WorkingCopyStatus {
    std::vector<StatusEntry> entries;

    [[nodiscard]] bool clean() const noexcept { return entries.empty(); }
    [[nodiscard]] std::size_t stagedCount() const noexcept;
    [[nodiscard]] std::size_t unstagedCount() const noexcept;
    [[nodiscard]] std::size_t conflictedCount() const noexcept;

    /// Lookup by exact path. Pair checking needs this constantly, and a linear
    /// scan per entry would be quadratic on a large working copy.
    [[nodiscard]] const StatusEntry* find(std::string_view path) const noexcept;
};

struct StatusOptions {
    bool includeUntracked = true;
    /// A build or cache directory can hold tens of thousands of files. Such a
    /// directory is normally gitignored, so
    /// recursing into ignored directories to confirm that is pure waste.
    bool includeIgnored = false;
    bool detectRenames = true;
};

[[nodiscard]] WorkingCopyStatus loadStatus(git_repository* repo, const StatusOptions& options = {});

} // namespace gity::git
