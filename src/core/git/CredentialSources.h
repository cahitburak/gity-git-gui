// Reading what the credential stores themselves hold.
//
// git's credential protocol answers "what is stored for this URL?" but has no
// way to list what is stored at all. Some stores can be read directly, and
// these are the parsers for the text ones: git's plain-text store file and
// `gh auth status`. The desktop keyring is read over D-Bus in the session
// layer. Nothing here ever returns a password.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gity::git {

/// One line of a `git credential-store` file, less its password.
struct StoredEntry {
    std::string protocol;
    std::string host;
    std::string path;
    std::string username;
    bool hasPassword = false;
};

/// Parses a credential-store file: one `scheme://user:password@host/path` URL
/// per line, percent-encoded. The password is noted, never kept.
[[nodiscard]] std::vector<StoredEntry> parseCredentialStore(std::string_view text);

/// One account `gh auth status` reports.
struct GhAccount {
    std::string host;
    std::string account;
    /// Where gh keeps the token: "keyring", or a file path.
    std::string storage;
    /// The account gh hands to git for this host.
    bool active = false;
};

/// Parses `gh auth status` output (2.40+ format, several accounts per host).
[[nodiscard]] std::vector<GhAccount> parseGhAuthStatus(std::string_view text);

/// Why GitHub CLI would give git nothing for a remote.
///
/// GitHub CLI's credential helper answers only for the account in use on a
/// host, and only when the remote URL names no user or names that account.
/// A URL naming anyone else gets no token, git falls back to a password
/// prompt, and GitHub refuses passwords — "Invalid username or token".
struct AccountMismatch {
    enum class Kind {
        None,
        /// The URL names a user GitHub CLI is not signed in as at all.
        UnknownUser,
        /// The URL names an account GitHub CLI holds but is not using.
        InactiveUser,
    };
    Kind kind = Kind::None;
    std::string urlUser;
    std::string activeAccount;
};

[[nodiscard]] AccountMismatch findAccountMismatch(std::string_view remoteUrl,
                                                  const std::vector<GhAccount>& accounts);

} // namespace gity::git
