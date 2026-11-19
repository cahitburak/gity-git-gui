// Reading file *contents* rather than diffs — what the image and asset
// views need.
//
// Core hands back bytes and says nothing about images: decoding is Qt's job
// and core stays Qt-free (the headless CI leg proves it). The split also puts
// the expensive part — decode — on the session thread where it belongs.
#pragma once

#include "Repository.h"

#include <cstddef>
#include <string>
#include <vector>

namespace gity::git {

struct BlobContent {
    std::vector<char> bytes;

    /// True when the blob is a Git LFS pointer rather than the file itself.
    /// Reading one as an image would show a 130-byte text file, so callers
    /// have to know the difference.
    bool lfsPointer = false;
    std::string lfsOid;      ///< From the pointer, when it is one.
    std::size_t lfsSize = 0; ///< Real size, from the pointer.

    /// Set by readWorktreeFile when it read the file. An empty file is read
    /// and empty; one it would not or could not read is empty and not found.
    bool found = false;

    [[nodiscard]] bool empty() const noexcept { return bytes.empty(); }

    [[nodiscard]] std::size_t size() const noexcept { return bytes.size(); }
};

/// The blob at `path` in `commitOid`. Empty when the path is absent there,
/// which is the normal case for an added file.
[[nodiscard]] BlobContent readBlobAt(git_repository* repo, const git_oid& commitOid,
                                     const std::string& path);

/// The blob at `path` in HEAD.
[[nodiscard]] BlobContent readBlobAtHead(git_repository* repo, const std::string& path);

/// The file as it is on disk. Not a blob at all, but the other half of every
/// working-copy comparison, and it keeps the callers symmetric.
/// Empty for a symlink, which is not followed, and for a file larger than
/// `maxBytes`. `path` is relative to `workdir` and has to stay inside it: an
/// absolute path, a "..", or a folder on the way that resolves outside the
/// working copy reads nothing.
[[nodiscard]] BlobContent readWorktreeFile(const std::string& workdir, const std::string& path,
                                           std::size_t maxBytes = std::size_t{512} << 20);

/// Parses an LFS pointer, filling `lfsOid` and `lfsSize`. Returns false when
/// the bytes are not a pointer.
bool parseLfsPointer(BlobContent& content);

} // namespace gity::git
