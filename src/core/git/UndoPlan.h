// Deciding what an undo may touch.
//
// An undo puts branches, tags and stashes back where an operation found them.
// The danger is doing that blindly: if a branch has moved since — a commit in
// a terminal, a pull in another tool — "back" would throw that newer work
// away. So a ref is restored only if it still holds exactly what the
// operation left in it, and anything else is reported as skipped.
//
// Pure functions over maps of names to object ids, so the rule can be tested
// without a repository.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace gity::git {

/// Full ref name → object id (hex).
using RefMap = std::map<std::string, std::string>;

struct RefChange {
    std::string ref;
    std::string from; ///< Empty: the ref does not exist now and is created.
    std::string to;   ///< Empty: the ref is deleted.
};

struct RefRestorePlan {
    std::vector<RefChange> changes;
    /// Refs the operation changed that have changed again since; left alone.
    std::vector<std::string> skipped;
};

/// What to change so every ref the operation touched is back at `before`,
/// given that the operation left `after` and the repository now holds
/// `current`.
[[nodiscard]] RefRestorePlan planRefRestore(const RefMap& before, const RefMap& after,
                                            const RefMap& current);

struct StashEntry {
    std::string oid;
    std::string message;
};

struct StashRestorePlan {
    /// Stashes the operation removed (a pop, a drop) that are still gone:
    /// stored back.
    std::vector<StashEntry> store;
    /// Stashes the operation created (a push) that are still there: dropped.
    std::vector<std::string> drop;
};

[[nodiscard]] StashRestorePlan planStashRestore(const std::vector<StashEntry>& before,
                                                const std::vector<StashEntry>& after,
                                                const std::vector<StashEntry>& current);

} // namespace gity::git
