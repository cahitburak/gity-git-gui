#include "AskpassPrompt.h"

#include <algorithm>
#include <cctype>

namespace gity::git {
namespace {

std::string lowered(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

/// The text between the first pair of single quotes, which is where git and
/// ssh both put the thing being asked about.
std::string quoted(std::string_view prompt) {
    const std::size_t open = prompt.find('\'');
    if (open == std::string_view::npos) {
        return {};
    }
    const std::size_t close = prompt.find('\'', open + 1);
    if (close == std::string_view::npos) {
        return {};
    }
    return std::string(prompt.substr(open + 1, close - open - 1));
}

} // namespace

AskpassRequest parseAskpassPrompt(std::string_view prompt) {
    AskpassRequest request;
    const std::string text = lowered(prompt);
    request.subject = quoted(prompt);

    // Checked before the others: the host-key question mentions neither a
    // password nor a username, but it does arrive on this channel and must not
    // fall through to the masked default.
    if (text.find("(yes/no") != std::string::npos ||
        text.find("authenticity of host") != std::string::npos) {
        request.kind = AskpassKind::HostKey;
        if (request.subject.empty()) {
            // ssh names the host without quotes here.
            const std::size_t start = text.find("host ");
            if (start != std::string::npos) {
                const std::size_t from = start + 5;
                const std::size_t end = text.find(' ', from);
                request.subject = std::string(prompt.substr(
                    from, end == std::string::npos ? std::string::npos : end - from));
            }
        }
        return request;
    }

    // Before "password": ssh says "Enter passphrase for key ...", and a
    // passphrase is worth naming correctly because it unlocks a local key
    // rather than authenticating to a server.
    if (text.find("passphrase") != std::string::npos) {
        request.kind = AskpassKind::Passphrase;
        return request;
    }
    if (text.find("password") != std::string::npos) {
        request.kind = AskpassKind::Password;
        return request;
    }
    if (text.find("username") != std::string::npos ||
        text.find("login") != std::string::npos) {
        request.kind = AskpassKind::Username;
        return request;
    }

    // Anything unrecognised is masked. Showing a secret in the clear because
    // the wording was unfamiliar is the worse of the two mistakes.
    request.kind = AskpassKind::Unknown;
    return request;
}

} // namespace gity::git
