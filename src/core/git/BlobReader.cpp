#include "BlobReader.h"

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <string_view>

namespace gity::git {
namespace {

using BlobHandle = Handle<git_blob, git_blob_free>;

constexpr std::string_view kLfsMagic = "version https://git-lfs.github.com/spec/";

/// The value after `key ` on its own line, as LFS pointers are formatted.
std::string_view pointerField(std::string_view text, std::string_view key) {
    std::size_t lineStart = 0;
    while (lineStart < text.size()) {
        std::size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string_view::npos) {
            lineEnd = text.size();
        }
        const std::string_view line = text.substr(lineStart, lineEnd - lineStart);
        if (line.compare(0, key.size(), key) == 0 && line.size() > key.size() &&
            line[key.size()] == ' ') {
            return line.substr(key.size() + 1);
        }
        lineStart = lineEnd + 1;
    }
    return {};
}

BlobContent fromBlob(git_blob* blob) {
    BlobContent content;
    const auto* raw = static_cast<const char*>(git_blob_rawcontent(blob));
    const auto size = static_cast<std::size_t>(git_blob_rawsize(blob));
    if (raw != nullptr && size > 0) {
        content.bytes.assign(raw, raw + size);
    }
    parseLfsPointer(content);
    return content;
}

} // namespace

bool parseLfsPointer(BlobContent& content) {
    // A pointer is a small text file. Anything large is the real thing, and
    // scanning a 40 MB image for a magic string would be waste.
    if (content.bytes.size() > 1024 || content.bytes.size() < kLfsMagic.size()) {
        return false;
    }
    const std::string_view text(content.bytes.data(), content.bytes.size());
    if (text.compare(0, kLfsMagic.size(), kLfsMagic) != 0) {
        return false;
    }

    content.lfsPointer = true;
    const std::string_view oid = pointerField(text, "oid");
    if (!oid.empty()) {
        content.lfsOid = std::string(oid);
    }
    const std::string_view size = pointerField(text, "size");
    if (!size.empty()) {
        content.lfsSize = static_cast<std::size_t>(std::strtoull(std::string(size).c_str(),
                                                                 nullptr, 10));
    }
    return true;
}

BlobContent readBlobAt(git_repository* repo, const git_oid& commitOid, const std::string& path) {
    CommitHandle commit;
    if (git_commit_lookup(commit.receive(), repo, &commitOid) < 0) {
        return {};
    }
    TreeHandle tree;
    if (git_commit_tree(tree.receive(), commit.get()) < 0) {
        return {};
    }

    git_tree_entry* rawEntry = nullptr;
    if (git_tree_entry_bypath(&rawEntry, tree.get(), path.c_str()) < 0) {
        return {}; // absent at this commit: an added file, not an error
    }
    const Handle<git_tree_entry, git_tree_entry_free> entry(rawEntry);

    if (git_tree_entry_type(entry.get()) != GIT_OBJECT_BLOB) {
        return {};
    }

    BlobHandle blob;
    if (git_blob_lookup(blob.receive(), repo, git_tree_entry_id(entry.get())) < 0) {
        return {};
    }
    return fromBlob(blob.get());
}

BlobContent readBlobAtHead(git_repository* repo, const std::string& path) {
    git_oid head{};
    if (git_reference_name_to_id(&head, repo, "HEAD") < 0) {
        return {};
    }
    return readBlobAt(repo, head, path);
}

BlobContent readWorktreeFile(const std::string& workdir, const std::string& path) {
    BlobContent content;
    std::error_code ec;
    const std::filesystem::path full = std::filesystem::path(workdir) / path;
    const auto size = std::filesystem::file_size(full, ec);
    if (ec) {
        return content;
    }

    std::ifstream in(full, std::ios::binary);
    if (!in) {
        return content;
    }
    content.bytes.resize(static_cast<std::size_t>(size));
    in.read(content.bytes.data(), static_cast<std::streamsize>(size));
    content.bytes.resize(static_cast<std::size_t>(in.gcount()));
    parseLfsPointer(content);
    return content;
}

} // namespace gity::git
