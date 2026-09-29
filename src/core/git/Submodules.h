// Submodules, for the sidebar group SPEC.md §0 asks for.
//
// Worth more than a list: a submodule sitting at a commit
// other than the one the superproject records is a common and confusing state
// — the files on disk are not what the branch says they are, and nothing in
// the main history shows it. The status is therefore carried alongside the
// name rather than left for the user to discover.
#pragma once

#include "core/git/Repository.h"

#include <string>
#include <vector>

namespace gity::git {

struct SubmoduleEntry {
    std::string name;
    std::string path;
    std::string url;
    /// Short OID of the commit the superproject records, or empty when the
    /// submodule has never been initialised.
    std::string recordedShortId;

    bool initialised = false;
    /// The checked-out commit differs from the one recorded.
    bool movedAway = false;
    /// The submodule's own working copy has changes.
    bool dirty = false;

    [[nodiscard]] bool needsAttention() const noexcept { return movedAway || dirty; }
};

[[nodiscard]] std::vector<SubmoduleEntry> listSubmodules(git_repository* repo);

} // namespace gity::git
