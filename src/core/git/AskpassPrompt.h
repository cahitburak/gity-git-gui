// Reading git's and ssh's credential prompts.
//
// git asks for credentials by running a helper program with the prompt as its
// only argument, and taking whatever the helper prints on stdout as the
// answer. The prompt is free text meant for a terminal, so the helper has to
// work out what is being asked before it can put up the right control — and
// getting that wrong has consequences beyond cosmetics:
//
//   * a username prompt shown with a masked field means typing a visible
//     thing blind;
//   * ssh's host-key question ("...(yes/no/[fingerprint])?") arrives through
//     the same channel. Answering it with a password field is useless, and
//     answering it automatically would defeat the check that exists to catch
//     a machine-in-the-middle.
//
// Qt-free and in core so the classification is tested against the prompts git
// and ssh actually emit.
#pragma once

#include <string>
#include <string_view>

namespace gity::git {

enum class AskpassKind {
    Username,   ///< Shown in the clear: it is not a secret.
    Password,   ///< Masked.
    Passphrase, ///< An ssh key passphrase. Masked.
    HostKey,    ///< A yes/no question. Never answered on the user's behalf.
    Unknown,    ///< Masked, because guessing wrong the other way leaks.
};

struct AskpassRequest {
    AskpassKind kind = AskpassKind::Unknown;
    /// The host or key the prompt names, when it names one. Empty otherwise.
    std::string subject;

    /// Whether the answer should be visible as it is typed.
    [[nodiscard]] bool echo() const noexcept {
        return kind == AskpassKind::Username || kind == AskpassKind::HostKey;
    }
};

[[nodiscard]] AskpassRequest parseAskpassPrompt(std::string_view prompt);

} // namespace gity::git
