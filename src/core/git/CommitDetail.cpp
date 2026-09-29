#include "CommitDetail.h"

#include <algorithm>

namespace gity::git {
namespace {

ChangeStatus statusOf(git_delta_t delta) noexcept {
    switch (delta) {
    case GIT_DELTA_ADDED:
        return ChangeStatus::Added;
    case GIT_DELTA_MODIFIED:
        return ChangeStatus::Modified;
    case GIT_DELTA_DELETED:
        return ChangeStatus::Deleted;
    case GIT_DELTA_RENAMED:
        return ChangeStatus::Renamed;
    case GIT_DELTA_COPIED:
        return ChangeStatus::Copied;
    case GIT_DELTA_TYPECHANGE:
        return ChangeStatus::TypeChanged;
    default:
        return ChangeStatus::Unknown;
    }
}

std::string trimmed(std::string_view text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(begin, end - begin + 1));
}

void fillSignatures(git_commit* commit, CommitDetail& detail) {
    if (const git_signature* author = git_commit_author(commit); author != nullptr) {
        detail.authorName = author->name != nullptr ? author->name : "";
        detail.authorEmail = author->email != nullptr ? author->email : "";
        detail.authorTime = author->when.time;
    }
    if (const git_signature* committer = git_commit_committer(commit); committer != nullptr) {
        detail.committerName = committer->name != nullptr ? committer->name : "";
        detail.committerEmail = committer->email != nullptr ? committer->email : "";
        detail.committerTime = committer->when.time;
    }
}

void fillMessage(git_commit* commit, CommitDetail& detail) {
    const char* summary = git_commit_summary(commit);
    detail.summary = summary != nullptr ? summary : "";

    const char* message = git_commit_message(commit);
    if (message == nullptr) {
        return;
    }
    // Everything after the first blank line is the body. Trailers stay in it —
    // Co-Authored-By and Signed-off-by are part of what a reviewer reads.
    std::string_view full(message);
    const auto breakAt = full.find("\n\n");
    if (breakAt != std::string_view::npos) {
        detail.body = trimmed(full.substr(breakAt + 2));
    }
}

void fillFiles(git_repository* repo, git_commit* commit, const CommitDetailOptions& options,
               CommitDetail& detail) {
    TreeHandle tree;
    if (git_commit_tree(tree.receive(), commit) < 0) {
        return;
    }

    TreeHandle parentTree;
    if (git_commit_parentcount(commit) > 0) {
        CommitHandle parent;
        if (git_commit_parent(parent.receive(), commit, 0) < 0) {
            return;
        }
        if (git_commit_tree(parentTree.receive(), parent.get()) < 0) {
            return;
        }
    }
    detail.files = listChangedFiles(repo, parentTree.get(), tree.get(), options,
                                    &detail.filesTruncated);
}

} // namespace

std::vector<ChangedFile> listChangedFiles(git_repository* repo, git_tree* fromTree,
                                          git_tree* toTree, const CommitDetailOptions& options,
                                          bool* truncated) {
    std::vector<ChangedFile> files;
    if (truncated != nullptr) {
        *truncated = false;
    }
    git_diff_options diffOptions;
    if (git_diff_options_init(&diffOptions, GIT_DIFF_OPTIONS_VERSION) < 0) {
        return files;
    }

    DiffHandle diff;
    if (git_diff_tree_to_tree(diff.receive(), repo, fromTree, toTree, &diffOptions) <
        0) {
        return files;
    }

    if (options.detectRenames) {
        git_diff_find_options findOptions;
        if (git_diff_find_options_init(&findOptions, GIT_DIFF_FIND_OPTIONS_VERSION) == 0) {
            findOptions.flags = GIT_DIFF_FIND_RENAMES;
            // Rename detection is worth it — a moved asset shows as a rename
            // rather than as an unrelated add and delete — but it is O(adds ×
            // deletes) and a failure here is not worth aborting the panel for.
            static_cast<void>(git_diff_find_similar(diff.get(), &findOptions));
        }
    }

    const std::size_t deltas = git_diff_num_deltas(diff.get());
    const std::size_t limit = std::min(deltas, options.maxFiles);
    if (truncated != nullptr) {
        *truncated = deltas > limit;
    }
    files.reserve(limit);

    for (std::size_t i = 0; i < limit; ++i) {
        const git_diff_delta* delta = git_diff_get_delta(diff.get(), i);
        if (delta == nullptr) {
            continue;
        }

        ChangedFile file;
        file.status = statusOf(delta->status);
        file.path = delta->new_file.path != nullptr ? delta->new_file.path : "";
        if (file.path.empty() && delta->old_file.path != nullptr) {
            file.path = delta->old_file.path;
        }
        if ((file.status == ChangeStatus::Renamed || file.status == ChangeStatus::Copied) &&
            delta->old_file.path != nullptr) {
            file.oldPath = delta->old_file.path;
        }
        file.binary = (delta->flags & GIT_DIFF_FLAG_BINARY) != 0;
        files.push_back(std::move(file));
    }
    return files;
}

char statusLetter(ChangeStatus status) noexcept {
    switch (status) {
    case ChangeStatus::Added:
        return 'A';
    case ChangeStatus::Modified:
        return 'M';
    case ChangeStatus::Deleted:
        return 'D';
    case ChangeStatus::Renamed:
        return 'R';
    case ChangeStatus::Copied:
        return 'C';
    case ChangeStatus::TypeChanged:
        return 'T';
    case ChangeStatus::Unknown:
        break;
    }
    return '?';
}

CommitDetail loadCommitDetail(git_repository* repo, const git_oid& oid,
                              const CommitDetailOptions& options) {
    CommitDetail detail;
    git_oid_cpy(&detail.oid, &oid);

    CommitHandle commit;
    check(git_commit_lookup(commit.receive(), repo, &oid), "look up the selected commit");

    fillSignatures(commit.get(), detail);
    fillMessage(commit.get(), detail);

    const unsigned int parentCount = git_commit_parentcount(commit.get());
    detail.parents.reserve(parentCount);
    for (unsigned int i = 0; i < parentCount; ++i) {
        detail.parents.push_back(*git_commit_parent_id(commit.get(), i));
    }

    fillFiles(repo, commit.get(), options, detail);
    return detail;
}

} // namespace gity::git
