// Which service a remote belongs to.
//
// Used for naming things the user recognises — "GitHub", not
// "github.com" — and, later, for knowing which OAuth endpoints to talk to
// (ADR-010). Recognition is by host, because that is the only thing a remote
// URL reliably carries.
//
// Self-hosted instances are the normal case, not the exception: a team is as
// likely to run GitLab or Gitea on its own domain as to use the public service. So the guess is a convenience and never a constraint — every
// provider can be chosen explicitly against any host.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gity::git {

enum class Provider {
    GitHub,
    GitLab,
    Bitbucket,
    Gitea,
    AzureDevOps,
    /// Anything else, including a self-hosted service this build has never
    /// heard of. Not an error state.
    Other,
};

/// Every provider, in the order a chooser should list them.
[[nodiscard]] std::vector<Provider> allProviders();

/// "GitHub", "GitLab", … — what a person calls it.
[[nodiscard]] std::string_view providerName(Provider provider);

/// The public instance, e.g. "github.com". Empty for Other.
[[nodiscard]] std::string_view defaultHost(Provider provider);

/// A guess from the host. Recognises the public instances exactly, and a
/// self-hosted instance only when the name says so — "gitlab.studio.com" is a
/// good guess, "git.studio.com" is not, and guessing there would be worse than
/// admitting the answer is unknown.
[[nodiscard]] Provider providerForHost(std::string_view host);

/// The host part of a remote URL: handles https://, ssh://, scp-style
/// (git@host:owner/repo) and a userinfo prefix. Empty for a local path, which
/// has no host and belongs to no provider.
[[nodiscard]] std::string hostOfRemote(std::string_view url);

} // namespace gity::git
