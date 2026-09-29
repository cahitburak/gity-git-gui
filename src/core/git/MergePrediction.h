// Predicting whether a merge would conflict, without touching anything.
//
// `git merge-tree --write-tree` (git 2.38+) performs the whole merge in memory
// and reports the conflicted paths. Nothing in the working copy, the index or
// any ref changes, so it can run before every pull, merge and rebase to say
// what will happen rather than finding out half way through.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gity::git {

struct MergePrediction {
    /// False when git is too old or the trial could not run; say nothing then.
    bool available = false;
    bool clean = false;
    std::vector<std::string> conflicts;
};

/// Parses `merge-tree --write-tree --name-only --no-messages -z` output:
/// the result tree, then each conflicted path, NUL-separated. Exit 0 is a
/// clean merge, 1 a conflicted one, anything else a failure to predict.
[[nodiscard]] MergePrediction parseMergeTree(int exitCode, std::string_view output);

/// Whether this git can make the prediction.
[[nodiscard]] bool mergeTreeSupported(int major, int minor) noexcept;

} // namespace gity::git
