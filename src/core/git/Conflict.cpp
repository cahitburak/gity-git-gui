#include "Conflict.h"

#include <git2.h>

#include <stdexcept>

namespace gity::git {
namespace {

using RefHandle = Handle<git_reference, git_reference_free>;

/// A git_buf given back however the scope is left.
struct Buffer {
    git_buf buf = GIT_BUF_INIT;

    Buffer() = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    ~Buffer() { git_buf_dispose(&buf); }
};

/// Whether a version can only be taken whole. A submodule's entry is a
/// commit, not a blob, and a symlink's is the path it points to: merging that
/// as text would save the link back as a regular file.
bool wholeOnly(std::uint32_t mode) {
    return mode != GIT_FILEMODE_BLOB && mode != GIT_FILEMODE_BLOB_EXECUTABLE;
}

/// A version's blob, or nothing when the version is missing or is not a file.
/// Marks the conflict whole-only for a version that is not a file, or not
/// text.
BlobHandle lookUpSide(git_repository* repo, const git_index_entry* entry, ConflictSide& side,
                      bool& binary) {
    BlobHandle blob;
    if (entry == nullptr) {
        return blob;
    }
    side.present = true;
    side.mode = entry->mode;
    if (wholeOnly(entry->mode)) {
        binary = true;
        return blob;
    }
    check(git_blob_lookup(blob.receive(), repo, &entry->id), "read a version of a conflicted file");
    if (git_blob_is_binary(blob.get()) != 0) {
        binary = true;
    }
    return blob;
}

/// The version's text as it is on disk: through the checkout filters.
std::string textOf(git_blob* blob, const std::string& path) {
    Buffer filtered;
    git_blob_filter_options options;
    check(git_blob_filter_options_init(&options, GIT_BLOB_FILTER_OPTIONS_VERSION),
          "initialize checkout filters");
    if (git_blob_filter(&filtered.buf, blob, path.c_str(), &options) == 0) {
        return filtered.buf.size > 0 ? std::string(filtered.buf.ptr, filtered.buf.size)
                                     : std::string();
    }
    // A filter that fails leaves the stored bytes, which are still the
    // version; only their line endings may differ from the disk's.
    const auto size = static_cast<std::size_t>(git_blob_rawsize(blob));
    return size > 0 ? std::string(static_cast<const char*>(git_blob_rawcontent(blob)), size)
                    : std::string();
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
    BlobHandle blobs[] = {lookUpSide(repo, ancestor, file.base, file.binary),
                          lookUpSide(repo, ours, file.ours, file.binary),
                          lookUpSide(repo, theirs, file.theirs, file.binary)};
    // The text only when it will be merged. A conflict that can only be taken
    // whole never shows it, and filtering a large binary's three versions is
    // all cost.
    if (!file.binary) {
        ConflictSide* sides[] = {&file.base, &file.ours, &file.theirs};
        for (std::size_t i = 0; i < 3; ++i) {
            if (blobs[i]) {
                sides[i]->text = textOf(blobs[i].get(), path);
            }
        }
    }

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
