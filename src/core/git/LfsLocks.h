// Git LFS file locks.
//
// Locking is the answer to the problem no merge tool solves: two people
// editing the same binary. A .psd or a baked lightmap cannot be merged at all,
// so the only way to avoid losing someone's afternoon is to stop the second
// edit from starting.
//
// The list comes from `git lfs locks --json`, which is the only machine
// format the tool offers. Parsed here rather than in the session layer so the
// parsing can be tested against real captured output — the response nests an
// owner object inside each element, and a flat key scan would attribute the
// wrong name to the wrong lock.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gity::git {

struct LfsLock {
    std::string id;
    std::string path;   ///< Repository-relative, as git-lfs reports it.
    std::string owner;  ///< The account holding it.
    std::string lockedAt;

    [[nodiscard]] bool valid() const noexcept { return !id.empty() && !path.empty(); }
};

/// Parses `git lfs locks --json`. Returns empty for anything unparseable,
/// which is the same thing an empty list means to a caller: nothing is locked
/// that we know of. Never throws — a lock list is advisory, and failing to
/// read it must not take a screen down.
[[nodiscard]] std::vector<LfsLock> parseLfsLocks(std::string_view json);

/// The lock on `path`, if any. Paths are compared exactly: git-lfs reports
/// them repository-relative with forward slashes on every platform.
[[nodiscard]] std::optional<LfsLock> lockFor(const std::vector<LfsLock>& locks,
                                             std::string_view path);

} // namespace gity::git
