// Choosing the account a repository uses, in that repository alone.
//
// git reads credential settings from the repository's own config after the
// global one, so a repository can override what the rest of the machine uses
// without touching it. Two forms:
//
//   * A host whose credentials come from GitHub CLI. gh's helper answers only
//     for its *active* account, so naming a user is not enough; the
//     repository instead gets a helper of its own that asks gh for the named
//     account's token — `gh auth token --user NAME`. gh still holds the token;
//     nothing is written down.
//   * Any other host: `credential.<url>.username`, which makes the configured
//     helper (a keyring, the store file) answer for that user.
//
// These build and recognise the values, so the rule is testable without a
// repository.
#pragma once

#include <string>
#include <string_view>

namespace gity::git {

/// GitHub-style account names: letters, digits and hyphens. Anything else is
/// refused before it can reach a shell command.
[[nodiscard]] bool isSafeAccountName(std::string_view name);

/// The repository-local helper that fetches `account`'s token from GitHub
/// CLI for `host`. Empty if the name is not safe.
[[nodiscard]] std::string ghAccountHelper(std::string_view host, std::string_view account);

/// The account a helper built by ghAccountHelper asks for; empty for any other
/// helper value.
[[nodiscard]] std::string accountOfGhHelper(std::string_view helper);

} // namespace gity::git
