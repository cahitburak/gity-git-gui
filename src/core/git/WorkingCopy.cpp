#include "WorkingCopy.h"

#include <algorithm>

namespace gity::git {
namespace {

FileState fromDelta(git_delta_t delta) noexcept {
    switch (delta) {
    case GIT_DELTA_ADDED:
        return FileState::Added;
    case GIT_DELTA_MODIFIED:
        return FileState::Modified;
    case GIT_DELTA_DELETED:
        return FileState::Deleted;
    case GIT_DELTA_RENAMED:
        return FileState::Renamed;
    case GIT_DELTA_TYPECHANGE:
        return FileState::TypeChanged;
    case GIT_DELTA_UNTRACKED:
        return FileState::Untracked;
    case GIT_DELTA_IGNORED:
        return FileState::Ignored;
    default:
        return FileState::Unchanged;
    }
}

/// The path a status entry is *about*. libgit2 reports both sides; the new
/// side is what the user sees in a file list, falling back to the old side for
/// a deletion, which has no new side at all.
std::string pathOf(const git_diff_delta* delta) {
    if (delta == nullptr) {
        return {};
    }
    if (delta->new_file.path != nullptr) {
        return delta->new_file.path;
    }
    return delta->old_file.path != nullptr ? delta->old_file.path : std::string();
}

} // namespace

char stateLetter(FileState state) noexcept {
    switch (state) {
    case FileState::Added:
        return 'A';
    case FileState::Modified:
        return 'M';
    case FileState::Deleted:
        return 'D';
    case FileState::Renamed:
        return 'R';
    case FileState::TypeChanged:
        return 'T';
    case FileState::Untracked:
        return '?';
    case FileState::Ignored:
        return '!';
    case FileState::Unchanged:
        break;
    }
    return ' ';
}

std::size_t WorkingCopyStatus::stagedCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        entries.begin(), entries.end(), [](const StatusEntry& e) { return e.hasStaged(); }));
}

std::size_t WorkingCopyStatus::unstagedCount() const noexcept {
    return static_cast<std::size_t>(
        std::count_if(entries.begin(), entries.end(), [](const StatusEntry& e) {
            return e.hasUnstaged() && !e.isUntracked();
        }));
}

std::size_t WorkingCopyStatus::conflictedCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        entries.begin(), entries.end(), [](const StatusEntry& e) { return e.conflicted; }));
}

const StatusEntry* WorkingCopyStatus::find(std::string_view path) const noexcept {
    // entries is kept sorted by path, so this is a binary search rather than
    // the quadratic scan pair checking would otherwise cause.
    const auto it = std::lower_bound(
        entries.begin(), entries.end(), path,
        [](const StatusEntry& entry, std::string_view target) { return entry.path < target; });
    if (it == entries.end() || it->path != path) {
        return nullptr;
    }
    return &*it;
}

WorkingCopyStatus loadStatus(git_repository* repo, const StatusOptions& options) {
    git_status_options statusOptions;
    check(git_status_options_init(&statusOptions, GIT_STATUS_OPTIONS_VERSION),
          "initialize status options");

    statusOptions.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    unsigned int flags = 0;
    if (options.includeUntracked) {
        flags |= GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;
    }
    if (options.includeIgnored) {
        flags |= GIT_STATUS_OPT_INCLUDE_IGNORED;
    }
    if (options.detectRenames) {
        flags |= GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX | GIT_STATUS_OPT_RENAMES_INDEX_TO_WORKDIR;
    }
    statusOptions.flags = flags;

    StatusListHandle list;
    check(git_status_list_new(list.receive(), repo, &statusOptions), "compute working copy status");

    WorkingCopyStatus out;
    const std::size_t count = git_status_list_entrycount(list.get());
    out.entries.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const git_status_entry* entry = git_status_byindex(list.get(), i);
        if (entry == nullptr) {
            continue;
        }

        StatusEntry parsed;
        parsed.conflicted = (entry->status & GIT_STATUS_CONFLICTED) != 0;

        if (entry->head_to_index != nullptr) {
            parsed.staged = fromDelta(entry->head_to_index->status);
            parsed.path = pathOf(entry->head_to_index);
            if (parsed.staged == FileState::Renamed &&
                entry->head_to_index->old_file.path != nullptr) {
                parsed.oldPath = entry->head_to_index->old_file.path;
            }
        }
        if (entry->index_to_workdir != nullptr) {
            parsed.unstaged = fromDelta(entry->index_to_workdir->status);
            if (parsed.path.empty()) {
                parsed.path = pathOf(entry->index_to_workdir);
            }
            if (parsed.unstaged == FileState::Renamed && parsed.oldPath.empty() &&
                entry->index_to_workdir->old_file.path != nullptr) {
                parsed.oldPath = entry->index_to_workdir->old_file.path;
            }
        }

        if (parsed.path.empty()) {
            continue;
        }
        out.entries.push_back(std::move(parsed));
    }

    std::sort(out.entries.begin(), out.entries.end(),
              [](const StatusEntry& a, const StatusEntry& b) { return a.path < b.path; });
    return out;
}

} // namespace gity::git
