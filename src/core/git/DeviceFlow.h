// The OAuth device authorization grant, RFC 8628 — ADR-010.
//
// The client asks the provider for a code, shows it, and opens a browser at a
// verification page. The user approves there; the client polls until a token
// comes back. No local listener, no client secret, and it works when the
// browser is on a different machine from the client.
//
// Only the parsing lives here, Qt-free and tested. The requests belong to the
// session layer, and the token belongs to git's credential helper — this file
// never sees one for longer than it takes to hand it on.
#pragma once

#include "core/git/Provider.h"

#include <string>
#include <string_view>

namespace gity::git {

/// Where a provider's device flow lives. Configurable rather than hardcoded:
/// a self-hosted GitLab or Gitea serves the same grant from its own domain.
struct DeviceFlowEndpoints {
    std::string codeUrl;   ///< POST here to start.
    std::string tokenUrl;  ///< POST here to poll.
    std::string scope;

    [[nodiscard]] bool valid() const { return !codeUrl.empty() && !tokenUrl.empty(); }
};

[[nodiscard]] DeviceFlowEndpoints endpointsFor(Provider provider, std::string_view host);

/// The provider's answer to the first request.
struct DeviceCode {
    std::string deviceCode;      ///< Sent back when polling. Not shown to anyone.
    std::string userCode;        ///< What the user types into the page.
    std::string verificationUrl; ///< Where they type it.
    int expiresInSeconds = 0;
    int intervalSeconds = 5;     ///< Minimum seconds between polls.

    [[nodiscard]] bool valid() const {
        return !deviceCode.empty() && !userCode.empty() && !verificationUrl.empty();
    }
};

[[nodiscard]] DeviceCode parseDeviceCode(std::string_view body);

/// Where a poll left things.
enum class PollState {
    Granted,
    /// The user has not finished yet. Keep polling.
    Pending,
    /// Polling too fast; the interval must grow.
    SlowDown,
    Denied,
    Expired,
    /// Anything unrecognised, including a transport failure.
    Failed,
};

struct PollResult {
    PollState state = PollState::Failed;
    /// Set only for Granted. Never logged, never stored by this client.
    std::string accessToken;
    /// The provider's own words, for anything that failed.
    std::string detail;
};

[[nodiscard]] PollResult parsePollResponse(std::string_view body);

} // namespace gity::git
