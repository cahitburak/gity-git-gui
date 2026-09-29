#include "Refs.h"

#include <algorithm>

namespace gity::git {
namespace {

std::string shorthandOf(git_reference* ref) {
    const char* name = git_reference_shorthand(ref);
    return name != nullptr ? std::string(name) : std::string();
}

std::string fullNameOf(git_reference* ref) {
    const char* name = git_reference_name(ref);
    return name != nullptr ? std::string(name) : std::string();
}

/// Resolves a ref to the commit it ultimately points at. Annotated tags point
/// at a tag object, not a commit, so they have to be peeled.
bool targetCommit(git_reference* ref, git_oid& out) {
    ObjectHandle peeled;
    if (git_reference_peel(peeled.receive(), ref, GIT_OBJECT_COMMIT) < 0) {
        return false;
    }
    git_oid_cpy(&out, git_object_id(peeled.get()));
    return true;
}

void fillUpstream(git_repository* repo, git_reference* branch, RefEntry& entry) {
    ReferenceHandle upstream;
    if (git_branch_upstream(upstream.receive(), branch) < 0) {
        // GIT_ENOTFOUND usually means the branch tracks nothing — the common
        // case for local-only work, not an error worth surfacing. The error
        // state is left alone deliberately: throwLastError is only ever called
        // immediately after a failing call, so a stale message is never read,
        // and git_error_clear is absent from some libgit2 builds' headers.
        //
        // Or it tracks a branch that is no longer there: the configuration
        // still names one, but the ref is gone.
        git_buf name = GIT_BUF_INIT;
        if (git_branch_upstream_name(&name, repo, git_reference_name(branch)) == 0) {
            const std::string full(name.ptr, name.size);
            git_buf_dispose(&name);
            git_reference* probe = nullptr;
            if (git_reference_lookup(&probe, repo, full.c_str()) < 0) {
                entry.upstreamGone = true;
                constexpr std::string_view remotes = "refs/remotes/";
                entry.upstream = full.rfind(remotes, 0) == 0 ? full.substr(remotes.size()) : full;
            } else {
                git_reference_free(probe);
            }
        }
        return;
    }

    entry.hasUpstream = true;
    entry.upstream = shorthandOf(upstream.get());

    git_oid upstreamOid{};
    if (!targetCommit(upstream.get(), upstreamOid)) {
        return;
    }

    std::size_t ahead = 0;
    std::size_t behind = 0;
    if (git_graph_ahead_behind(&ahead, &behind, repo, &entry.target, &upstreamOid) < 0) {
        return;
    }
    entry.ahead = ahead;
    entry.behind = behind;
}

void loadBranches(git_repository* repo, git_branch_t which, RefSet& out) {
    BranchIteratorHandle iterator;
    if (git_branch_iterator_new(iterator.receive(), repo, which) < 0) {
        throwLastError(-1, "enumerate branches");
    }

    while (true) {
        git_reference* rawBranch = nullptr;
        git_branch_t branchType = GIT_BRANCH_ALL;
        const int rc = git_branch_next(&rawBranch, &branchType, iterator.get());
        if (rc == GIT_ITEROVER) {
            break;
        }
        if (rc < 0) {
            throwLastError(rc, "read the next branch");
        }
        const ReferenceHandle branch(rawBranch);

        RefEntry entry;
        entry.kind = (which == GIT_BRANCH_LOCAL) ? RefKind::LocalBranch : RefKind::RemoteBranch;
        entry.name = shorthandOf(branch.get());
        entry.fullName = fullNameOf(branch.get());
        if (!targetCommit(branch.get(), entry.target)) {
            continue;
        }

        if (which == GIT_BRANCH_LOCAL) {
            entry.isHead = git_branch_is_head(branch.get()) == 1;
            fillUpstream(repo, branch.get(), entry);
            out.localBranches.push_back(std::move(entry));
        } else {
            // origin/HEAD is a symbolic alias for another branch; listing it
            // just duplicates a row the user already sees.
            // What it points at is worth keeping, though: it is the remote's
            // default branch.
            if (entry.name.size() >= 5 &&
                entry.name.compare(entry.name.size() - 5, 5, "/HEAD") == 0) {
                if (git_reference_type(branch.get()) == GIT_REFERENCE_SYMBOLIC) {
                    if (const char* target = git_reference_symbolic_target(branch.get())) {
                        constexpr std::string_view prefix = "refs/remotes/";
                        const std::string_view full = target;
                        if (full.substr(0, prefix.size()) == prefix) {
                            out.remoteDefaults.emplace_back(full.substr(prefix.size()));
                        }
                    }
                }
                continue;
            }
            out.remoteBranches.push_back(std::move(entry));
        }
    }
}

void loadTags(git_repository* repo, RefSet& out) {
    git_strarray names{};
    if (git_tag_list(&names, repo) < 0) {
        throwLastError(-1, "list tags");
    }

    for (std::size_t i = 0; i < names.count; ++i) {
        const std::string shorthand = names.strings[i];
        const std::string full = "refs/tags/" + shorthand;

        ReferenceHandle ref;
        if (git_reference_lookup(ref.receive(), repo, full.c_str()) < 0) {
            continue;
        }

        RefEntry entry;
        entry.kind = RefKind::Tag;
        entry.name = shorthand;
        entry.fullName = full;
        if (targetCommit(ref.get(), entry.target)) {
            out.tags.push_back(std::move(entry));
        }
    }
    git_strarray_dispose(&names);
}

int stashCallback(std::size_t index, const char* message, const git_oid* stashId, void* payload) {
    auto* out = static_cast<RefSet*>(payload);

    RefEntry entry;
    entry.kind = RefKind::Stash;
    entry.name = "stash@{" + std::to_string(index) + "}";
    entry.message = message != nullptr ? message : "";
    if (stashId != nullptr) {
        git_oid_cpy(&entry.target, stashId);
    }
    out->stashes.push_back(std::move(entry));
    return 0;
}

void loadHead(git_repository* repo, RefSet& out) {
    ReferenceHandle head;
    const int rc = git_repository_head(head.receive(), repo);
    if (rc == GIT_EUNBORNBRANCH) {
        out.headUnborn = true;
        out.headName = "(no commits yet)";
        return;
    }
    if (rc < 0) {
        out.headName = "(unknown)";
        return;
    }

    out.headDetached = git_repository_head_detached(repo) == 1;
    if (out.headDetached) {
        git_oid oid{};
        if (targetCommit(head.get(), oid)) {
            char buffer[GIT_OID_SHA1_HEXSIZE + 1] = {};
            git_oid_tostr(buffer, sizeof(buffer), &oid);
            out.headName = std::string(buffer, 8);
        } else {
            out.headName = "(detached)";
        }
    } else {
        out.headName = shorthandOf(head.get());
    }
}

bool isDigit(char c) noexcept {
    return c >= '0' && c <= '9';
}

} // namespace

bool versionLess(std::string_view a, std::string_view b) noexcept {
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() && j < b.size()) {
        if (isDigit(a[i]) && isDigit(b[j])) {
            // Compare the two numbers by value without converting them, so a
            // run too long for any integer type still orders correctly:
            // leading zeros dropped, then the longer run is the larger, then
            // digit by digit.
            std::size_t aStart = i;
            std::size_t bStart = j;
            while (aStart < a.size() && a[aStart] == '0') {
                ++aStart;
            }
            while (bStart < b.size() && b[bStart] == '0') {
                ++bStart;
            }
            std::size_t aEnd = aStart;
            std::size_t bEnd = bStart;
            while (aEnd < a.size() && isDigit(a[aEnd])) {
                ++aEnd;
            }
            while (bEnd < b.size() && isDigit(b[bEnd])) {
                ++bEnd;
            }
            const std::size_t aLength = aEnd - aStart;
            const std::size_t bLength = bEnd - bStart;
            if (aLength != bLength) {
                return aLength < bLength;
            }
            if (const int order = a.substr(aStart, aLength).compare(b.substr(bStart, bLength));
                order != 0) {
                return order < 0;
            }
            // Equal values: fewer leading zeros first, so the order is total
            // and `v01` and `v1` do not compare equal.
            if (aStart - i != bStart - j) {
                return aStart - i < bStart - j;
            }
            i = aEnd;
            j = bEnd;
            continue;
        }
        if (a[i] != b[j]) {
            return static_cast<unsigned char>(a[i]) < static_cast<unsigned char>(b[j]);
        }
        ++i;
        ++j;
    }
    return a.size() - i < b.size() - j;
}

namespace {

void markDefaults(RefSet& out) {
    const auto isRemoteDefault = [&out](const std::string& name) {
        return std::find(out.remoteDefaults.begin(), out.remoteDefaults.end(), name) !=
               out.remoteDefaults.end();
    };
    for (auto& entry : out.remoteBranches) {
        entry.isDefault = isRemoteDefault(entry.name);
    }
    for (auto& entry : out.localBranches) {
        entry.isDefault = entry.hasUpstream ? isRemoteDefault(entry.upstream)
                                            : isRemoteDefault("origin/" + entry.name);
    }
}

} // namespace

RefSet loadRefs(git_repository* repo) {
    RefSet out;

    loadHead(repo, out);
    loadBranches(repo, GIT_BRANCH_LOCAL, out);
    loadBranches(repo, GIT_BRANCH_REMOTE, out);
    loadTags(repo, out);
    markDefaults(out);

    // A repository that has never stashed has no stash reflog; that is normal.
    static_cast<void>(git_stash_foreach(repo, stashCallback, &out));

    // Numbers by value, so `feature-2` comes before `feature-10`; otherwise
    // alphabetical. Branches ascend — unlike tags, a name is not a release and
    // the newest is no likelier to be the one wanted.
    const auto byName = [](const RefEntry& a, const RefEntry& b) {
        return versionLess(a.name, b.name);
    };
    std::sort(out.remoteBranches.begin(), out.remoteBranches.end(), byName);
    // Tags are mostly versions: ordered by value, so `v1.10` follows `v1.9`
    // rather than landing between `v1.1` and `v1.2`, and newest first — the
    // release you are looking for is usually the latest one.
    std::sort(out.tags.begin(), out.tags.end(), [](const RefEntry& a, const RefEntry& b) {
        return versionLess(b.name, a.name);
    });

    // The checked-out branch sorts first; everything else alphabetically. It is
    // the row the user looks for, so it should not move as branches are added.
    std::sort(out.localBranches.begin(), out.localBranches.end(),
              [](const RefEntry& a, const RefEntry& b) {
                  if (a.isHead != b.isHead) {
                      return a.isHead;
                  }
                  return versionLess(a.name, b.name);
              });

    out.submodules = listSubmodules(repo);
    return out;
}

} // namespace gity::git
