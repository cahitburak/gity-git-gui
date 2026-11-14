#include "Conflict.h"

#include <git2.h>

#include <stdexcept>

namespace gity::git {
namespace {

using BlobHandle = Handle<git_blob, git_blob_free>;
using RefHandle = Handle<git_reference, git_reference_free>;

ConflictSide readSide(git_repository* repo, const git_index_entry* entry, const std::string& path,
                      bool& binary) {
    ConflictSide side;
    if (entry == nullptr) {
        return side;
    }
    side.present = true;
    BlobHandle blob;
    check(git_blob_lookup(blob.receive(), repo, &entry->id), "read a version of a conflicted file");
    if (git_blob_is_binary(blob.get()) != 0) {
        binary = true;
    }
    git_buf filtered = GIT_BUF_INIT;
    git_blob_filter_options options = GIT_BLOB_FILTER_OPTIONS_INIT;
    const int rc = git_blob_filter(&filtered, blob.get(), path.c_str(), &options);
    if (rc == 0) {
        side.text.assign(filtered.ptr, filtered.size);
    } else {
        // A filter that fails leaves the stored bytes, which are still the
        // version; only their line endings may differ from the disk's.
        const auto* raw = static_cast<const char*>(git_blob_rawcontent(blob.get()));
        side.text.assign(raw, static_cast<std::size_t>(git_blob_rawsize(blob.get())));
    }
    git_buf_dispose(&filtered);
    return side;
}

ConflictCommit describe(git_repository* repo, const char* refName) {
    ConflictCommit found;
    git_oid id{};
    // Missing is an answer here: no MERGE_HEAD outside a merge.
    if (git_reference_name_to_id(&id, repo, refName) != 0) {
        return found;
    }
    char hex[GIT_OID_SHA1_HEXSIZE + 1] = {};
    git_oid_tostr(hex, 8, &id);
    found.shortId = hex;
    CommitHandle commit;
    if (git_commit_lookup(commit.receive(), repo, &id) == 0) {
        if (const char* summary = git_commit_summary(commit.get()); summary != nullptr) {
            found.summary = summary;
        }
    }
    return found;
}

} // namespace

ConflictFile loadConflict(git_repository* repo, const std::string& path) {
    IndexHandle index;
    check(git_repository_index(index.receive(), repo), "open the index");
    // The index on disk, not one cached from before the conflict.
    check(git_index_read(index.get(), 0), "read the index");

    const git_index_entry* ancestor = nullptr;
    const git_index_entry* ours = nullptr;
    const git_index_entry* theirs = nullptr;
    const int rc = git_index_conflict_get(&ancestor, &ours, &theirs, index.get(), path.c_str());
    if (rc == GIT_ENOTFOUND) {
        throw std::runtime_error(path + " is not conflicted any more.");
    }
    check(rc, "read a conflicted file's versions");

    ConflictFile file;
    file.path = path;
    file.base = readSide(repo, ancestor, path, file.binary);
    file.ours = readSide(repo, ours, path, file.binary);
    file.theirs = readSide(repo, theirs, path, file.binary);

    file.head = describe(repo, "HEAD");
    RefHandle head;
    if (git_repository_head(head.receive(), repo) == 0 &&
        git_reference_is_branch(head.get()) != 0) {
        file.head.branch = git_reference_shorthand(head.get());
    }
    for (const char* name : {"MERGE_HEAD", "CHERRY_PICK_HEAD", "REVERT_HEAD", "REBASE_HEAD"}) {
        file.incoming = describe(repo, name);
        if (!file.incoming.shortId.empty()) {
            break;
        }
    }

    if (!file.binary && file.bothSides()) {
        file.merge = mergeLines(file.base.text, file.ours.text, file.theirs.text);
    }
    return file;
}

} // namespace gity::git
