// The `git credential` wire format: `key=value` lines, ended by a blank line.
//
// Gity keeps no credentials of its own (ADR-010). To show or change the one
// git is using, it asks git — `git credential fill` returns what the user's
// helper would hand over for a URL, `approve` stores, `reject` erases — and
// these functions are the two ends of that conversation.
#pragma once

#include <string>
#include <string_view>

namespace gity::git {

struct Credential {
    std::string protocol;
    std::string host;
    std::string path;
    std::string username;
    std::string password;

    [[nodiscard]] bool hasSecret() const { return !password.empty(); }
};

/// What git's credential helpers need to identify a remote: the protocol,
/// host (with port), and — only when `useHttpPath` is set, as git itself does
/// — the path. A username in the URL is carried over, since it selects which
/// stored credential answers. False for URLs with no credential to speak of:
/// SSH, local paths, file://.
[[nodiscard]] bool credentialForUrl(std::string_view url, bool useHttpPath, Credential* out);

/// Parses `git credential fill` output. Unknown keys are ignored.
[[nodiscard]] Credential parseCredential(std::string_view text);

/// The request or record to write to `git credential`, blank-line terminated.
/// Empty fields are left out.
[[nodiscard]] std::string serializeCredential(const Credential& credential);

/// git refuses a newline or NUL in any field — and a newline would let one
/// field smuggle in another. Checked before anything is written.
[[nodiscard]] bool isWritable(const Credential& credential);

} // namespace gity::git
