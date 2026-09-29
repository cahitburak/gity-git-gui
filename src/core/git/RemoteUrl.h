// Deriving a folder name from a remote URL.
//
// Small, and wrong in more ways than it looks: remotes arrive as https URLs,
// as scp-style SSH addresses with no scheme and a colon where the path starts,
// as local paths, and with or without a trailing slash or ".git". Getting it
// wrong means offering to clone into a folder named after the host, or into
// one called ".git". It lives in core so it can be tested.
#pragma once

#include <string>
#include <string_view>

namespace gity::git {

/// The folder git itself would create for `url`: the last path segment with a
/// trailing ".git" removed. Empty when there is nothing to take.
[[nodiscard]] std::string folderNameForRemote(std::string_view url);

/// `url` with `username` as its userinfo — how a repository is bound to one
/// account when a host has several, and the fix for the case where a work
/// repository is reached with personal credentials.
///
/// Returns the URL unchanged when there is nothing to attach a username to: a
/// local path has no host, and an scp-style remote's `git@` is the ssh user
/// rather than an account name, so rewriting it would break the remote.
[[nodiscard]] std::string remoteUrlWithUser(std::string_view url, std::string_view username);

} // namespace gity::git
