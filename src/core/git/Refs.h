// The refs sidebar's data, loaded on the session thread (ADR-004).
//
// Enumerating refs is cheap next to the history walk, so this is a single
// synchronous pass rather than a stream. It still runs off the UI thread,
// because ahead/behind is a graph query and a repository with many remote
// branches makes it a real one.
#pragma once

#include "Repository.h"
#include "Submodules.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace gity::git {

enum class RefKind {
    LocalBranch,
    RemoteBranch,
    Tag,
    Stash,
};

struct RefEntry {
    RefKind kind = RefKind::LocalBranch;
    std::string name;     ///< Shorthand: "main", "origin/main", "v1.2".
    std::string fullName; ///< "refs/heads/main". Empty for stashes.
    git_oid target{};

    bool isHead = false; ///< This is the checked-out branch.

    /// The remote's default branch — what its HEAD names, as recorded in
    /// <remote>/HEAD — or a local branch that tracks it (or, tracking
    /// nothing, shares its name with origin's).
    bool isDefault = false;

    /// Upstream tracking, local branches only. `hasUpstream` is false when the
    /// branch tracks nothing, which is different from being level with it.
    bool hasUpstream = false;
    std::string upstream;
    /// The branch is set to track `upstream`, but that branch no longer
    /// exists — deleted on the remote and pruned by a fetch. git calls this
    /// "gone". `hasUpstream` is false: there is nothing to be ahead of.
    bool upstreamGone = false;
    std::size_t ahead = 0;
    std::size_t behind = 0;

    /// Stash message, stashes only.
    std::string message;
};

struct RefSet {
    std::vector<RefEntry> localBranches;
    std::vector<RefEntry> remoteBranches;
    std::vector<RefEntry> tags;
    std::vector<RefEntry> stashes;
    std::vector<SubmoduleEntry> submodules;
    /// Each remote's default branch, as "origin/main" — from <remote>/HEAD,
    /// which clone sets and fetch can keep current.
    std::vector<std::string> remoteDefaults;

    std::string headName;      ///< Branch shorthand, or a short OID when detached.
    bool headDetached = false;
    bool headUnborn = false;   ///< Fresh repository with no commits yet.

    [[nodiscard]] std::size_t total() const noexcept {
        return localBranches.size() + remoteBranches.size() + tags.size() + stashes.size();
    }
};

/// Enumerates every ref in the repository, with ahead/behind for local
/// branches that track an upstream.
[[nodiscard]] RefSet loadRefs(git_repository* repo);

/// Orders names the way a person reads version numbers: runs of digits
/// compare by value, so `v1.9` comes before `v1.10` and `build-2` before
/// `build-10`. Everything else compares character by character.
///
/// A shorter name that is a prefix of a longer one sorts first, so `v1.0`
/// precedes `v1.0-rc1` — the same order `git tag --sort=version:refname`
/// gives without a `versionsort.suffix` configured.
[[nodiscard]] bool versionLess(std::string_view a, std::string_view b) noexcept;

} // namespace gity::git
