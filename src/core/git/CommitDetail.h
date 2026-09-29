// What the detail panel shows for one selected commit.
//
// Loaded on demand on the session thread, because the changed-file list is a
// tree-to-tree diff and a large merge makes that expensive. Selecting rows
// quickly with the arrow keys must not block the UI, so requests supersede:
// only the newest selection's result is worth having.
#pragma once

#include "Repository.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gity::git {

enum class ChangeStatus {
    Added,
    Modified,
    Deleted,
    Renamed,
    Copied,
    TypeChanged,
    Unknown,
};

[[nodiscard]] char statusLetter(ChangeStatus status) noexcept;

struct ChangedFile {
    std::string path;
    std::string oldPath; ///< Set only for renames and copies.
    ChangeStatus status = ChangeStatus::Unknown;
    bool binary = false;
};

struct CommitDetail {
    git_oid oid{};
    std::string summary;
    std::string body; ///< Message after the summary line, trimmed.
    std::string authorName;
    std::string authorEmail;
    std::int64_t authorTime = 0;
    std::string committerName;
    std::string committerEmail;
    std::int64_t committerTime = 0;

    std::vector<git_oid> parents;
    std::vector<ChangedFile> files;

    /// True when the file list was capped. A merge of two long-lived branches
    /// can touch tens of thousands of paths, and a panel nobody scrolls to the
    /// bottom of is not worth the wait.
    bool filesTruncated = false;

    [[nodiscard]] bool isMerge() const noexcept { return parents.size() > 1; }
    [[nodiscard]] bool isRoot() const noexcept { return parents.empty(); }
};

struct CommitDetailOptions {
    /// Diffs against the first parent, which is what a person means by "what
    /// changed in this commit" for a merge.
    std::size_t maxFiles = 5000;
    bool detectRenames = true;
};

/// The files that differ between two trees, `fromTree` to `toTree`, with
/// renames detected. Either tree may be null, meaning empty. `truncated`, when
/// given, says whether the list stopped at `options.maxFiles`.
[[nodiscard]] std::vector<ChangedFile> listChangedFiles(git_repository* repo,
                                                        git_tree* fromTree, git_tree* toTree,
                                                        const CommitDetailOptions& options,
                                                        bool* truncated = nullptr);

[[nodiscard]] CommitDetail loadCommitDetail(git_repository* repo, const git_oid& oid,
                                            const CommitDetailOptions& options = {});

} // namespace gity::git
