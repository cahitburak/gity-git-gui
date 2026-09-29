// Where to open a new pull request (or merge request) in the browser.
//
// Gity does not create pull requests through an API: that would need a token
// Gity would have to hold (ADR-010 says it never does), and the web page the
// host already provides is where people write the description and pick
// reviewers anyway. So this only builds the address of that page, from the
// remote URL and the two branch names.
#pragma once

#include "core/git/Provider.h"

#include <optional>
#include <string>
#include <string_view>

namespace gity::git {

/// The repository's web page, from a remote URL: https://, ssh:// and
/// scp-style (git@host:owner/repo.git) all give "https://host/owner/repo". An
/// https port is kept; an ssh port is not, since it is not the web server's.
/// Azure DevOps ssh remotes are mapped to their https layout. Empty for a
/// local path or anything without a host.
[[nodiscard]] std::string repositoryWebUrl(std::string_view remoteUrl);

/// The page that starts a pull request from `source` into `target`, both
/// branch names as the remote knows them ("feature/x", not "origin/feature/x").
/// An empty `target` leaves it to the host, which picks the default branch.
/// Nothing for Provider::Other: there is no address to guess at.
[[nodiscard]] std::optional<std::string> newPullRequestUrl(Provider provider,
                                                           std::string_view remoteUrl,
                                                           std::string_view source,
                                                           std::string_view target);

/// What the host calls it: "merge request" on GitLab, "pull request" elsewhere.
[[nodiscard]] std::string_view pullRequestNoun(Provider provider);

} // namespace gity::git
