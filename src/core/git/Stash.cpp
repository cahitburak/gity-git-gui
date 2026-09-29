#include "Stash.h"

namespace gity::git {
namespace {

using SignatureHandle = Handle<git_signature, git_signature_free>;

int collectStash(std::size_t index, const char* message, const git_oid* stashId, void* payload) {
    auto* out = static_cast<std::vector<StashEntry>*>(payload);
    StashEntry entry;
    entry.index = index;
    entry.message = message != nullptr ? message : "";
    if (stashId != nullptr) {
        git_oid_cpy(&entry.target, stashId);
    }
    out->push_back(std::move(entry));
    return 0;
}

/// Stash needs a committer. Falling back to a placeholder rather than failing
/// keeps the feature usable in a repository with no user.name configured —
/// which is common on a machine someone has just cloned onto.
SignatureHandle stashSignature(git_repository* repo) {
    SignatureHandle signature;
    if (git_signature_default(signature.receive(), repo) == 0) {
        return signature;
    }
    check(git_signature_now(signature.receive(), "Gity", "gity@localhost"),
          "create a stash signature");
    return signature;
}

} // namespace

std::vector<StashEntry> listStashes(git_repository* repo) {
    std::vector<StashEntry> entries;
    // A repository that has never stashed has no stash reflog; that is normal.
    static_cast<void>(git_stash_foreach(repo, collectStash, &entries));
    return entries;
}

bool stashSave(git_repository* repo, const std::string& message, const StashOptions& options,
               git_oid* stashId) {
    const SignatureHandle signature = stashSignature(repo);

    unsigned int flags = GIT_STASH_DEFAULT;
    if (options.includeUntracked) {
        flags |= GIT_STASH_INCLUDE_UNTRACKED;
    }
    if (options.includeIgnored) {
        flags |= GIT_STASH_INCLUDE_IGNORED;
    }
    if (options.keepIndex) {
        flags |= GIT_STASH_KEEP_INDEX;
    }

    git_oid local{};
    const int rc = git_stash_save(&local, repo, signature.get(), message.c_str(), flags);
    if (rc == GIT_ENOTFOUND) {
        return false; // nothing to stash is not a failure
    }
    check(rc, "stash the working copy");
    if (stashId != nullptr) {
        git_oid_cpy(stashId, &local);
    }
    return true;
}

void stashApply(git_repository* repo, std::size_t index) {
    git_stash_apply_options options;
    check(git_stash_apply_options_init(&options, GIT_STASH_APPLY_OPTIONS_VERSION),
          "initialize stash apply options");
    check(git_stash_apply(repo, index, &options), "apply the stash");
}

void stashPop(git_repository* repo, std::size_t index) {
    git_stash_apply_options options;
    check(git_stash_apply_options_init(&options, GIT_STASH_APPLY_OPTIONS_VERSION),
          "initialize stash apply options");
    // git_stash_pop drops only on a clean apply, which is the behaviour we
    // want: a conflicted pop must leave the stash recoverable.
    check(git_stash_pop(repo, index, &options), "pop the stash");
}

void stashDrop(git_repository* repo, std::size_t index) {
    check(git_stash_drop(repo, index), "drop the stash");
}

} // namespace gity::git
