// Two refs side by side: what differs between their tips, and how their
// histories relate.
//
// The diff is tip to tip — the tree at `from` against the tree at `to` — which
// is what "the difference between these branches" literally is. The commit
// lists and counts answer the other half of the question: which side has work
// the other lacks, and where they parted.
#pragma once

#include "CommitDetail.h"
#include "Repository.h"

#include <cstddef>
#include <string>
#include <vector>

namespace gity::git {

struct ComparedCommit {
    git_oid oid{};
    std::string summary;
};

struct Comparison {
    git_oid from{};
    git_oid to{};

    std::vector<ChangedFile> files;
    bool filesTruncated = false;

    /// Commits reachable from `to` but not `from`, and the reverse, newest
    /// first. Counted in full; listed only up to CompareOptions::maxCommits.
    std::size_t onlyInTo = 0;
    std::size_t onlyInFrom = 0;
    std::vector<ComparedCommit> onlyInToCommits;
    std::vector<ComparedCommit> onlyInFromCommits;

    /// Where the two histories meet. False for unrelated histories.
    bool hasMergeBase = false;
    git_oid mergeBase{};
};

struct CompareOptions {
    std::size_t maxCommits = 50;
    CommitDetailOptions files;
};

[[nodiscard]] Comparison loadComparison(git_repository* repo, const git_oid& from,
                                        const git_oid& to, const CompareOptions& options = {});

} // namespace gity::git
