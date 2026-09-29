// The repository's remotes.
//
// Listing is a thin read over libgit2. The part worth having in core is the
// name check: git will reject a bad remote name itself, but only after the
// dialog has been dismissed, and a dialog that accepts input git is certain
// to refuse is a dialog that wastes the user's time to tell them nothing.
#pragma once

#include "core/git/Repository.h"

#include <string>
#include <string_view>
#include <vector>

namespace gity::git {

struct RemoteEntry {
    std::string name;
    std::string fetchUrl;
    std::string pushUrl; ///< Equal to fetchUrl unless a push URL is set.

    [[nodiscard]] bool pushesElsewhere() const noexcept {
        return !pushUrl.empty() && pushUrl != fetchUrl;
    }
};

[[nodiscard]] std::vector<RemoteEntry> listRemotes(git_repository* repo);

enum class RemoteNameProblem {
    None,
    Empty,
    Whitespace,
    /// git stores remotes as ref namespaces, so the characters refs forbid are
    /// forbidden here for the same reason.
    IllegalCharacter,
    LeadingOrTrailingDot,
    AlreadyExists,
};

/// Checks `name` against git's rules and the names already in use.
[[nodiscard]] RemoteNameProblem checkRemoteName(std::string_view name,
                                                const std::vector<RemoteEntry>& existing);

} // namespace gity::git
