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

/// `text` with the secret in every URL's userinfo replaced by `***`, for
/// anything a person will see: the command log, an error message, a list of
/// remotes. Handles both forms a secret takes in a remote URL:
///
///   * `https://user:secret@host/...` becomes `https://user:***@host/...`;
///   * `https://ghp_xxxx@host/...` — a token as the username, which is how
///     GitHub, GitLab and Bitbucket document cloning with one — becomes
///     `https://***@host/...`, and `https://ghp_xxxx:x-oauth-basic@host/...`
///     becomes `https://***:***@host/...`.
///
/// A plain username (`https://alice@host`) is not a secret and is kept.
[[nodiscard]] std::string maskUrlCredentials(std::string_view text);

/// Undoes maskUrlCredentials on a URL a person edited from its masked form,
/// taking the secret back from `original`. Without it, accepting an edited
/// URL would save the literal `***` over the real token.
///
/// Only while the edit still names the same host over the same scheme (or
/// upgrades http to https): pointed anywhere else, the masked parts are
/// dropped rather than the secret sent to a server it was not meant for.
[[nodiscard]] std::string unmaskUrlCredentials(std::string_view edited, std::string_view original);

} // namespace gity::git
