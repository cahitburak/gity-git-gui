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
[[nodiscard]] BlobContent readWorktreeFile(const std::string& workdir, const std::string& path);

/// Parses an LFS pointer, filling `lfsOid` and `lfsSize`. Returns false when
/// the bytes are not a pointer.
bool parseLfsPointer(BlobContent& content);

} // namespace gity::git
