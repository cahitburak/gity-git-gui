#include "RemoteUrl.h"

#include "core/git/Provider.h"

#include <algorithm>
#include <cctype>

namespace gity::git {

std::string folderNameForRemote(std::string_view url) {
    // Leading and trailing whitespace comes from pasting.
    while (!url.empty() && std::isspace(static_cast<unsigned char>(url.front())) != 0) {
        url.remove_prefix(1);
    }
    while (!url.empty() && (std::isspace(static_cast<unsigned char>(url.back())) != 0 ||
                            url.back() == '/')) {
        url.remove_suffix(1);
    }
    if (url.empty()) {
        return {};
    }

    // scp-style remotes (git@host:owner/repo.git) have no scheme, so the last
    // segment starts after ':' as well as after '/'. Taking the later of the
    // two handles both, and handles a colon inside an https URL's port because
    // a path separator always follows it.
    const std::size_t slash = url.find_last_of('/');
    const std::size_t colon = url.find_last_of(':');
    std::size_t start = 0;
    if (slash != std::string_view::npos && colon != std::string_view::npos) {
        start = std::max(slash, colon) + 1;
    } else if (slash != std::string_view::npos) {
        start = slash + 1;
    } else if (colon != std::string_view::npos) {
        start = colon + 1;
    }

    std::string name(url.substr(start));

    constexpr std::string_view kSuffix = ".git";
    if (name.size() > kSuffix.size()) {
        std::string tail = name.substr(name.size() - kSuffix.size());
        std::transform(tail.begin(), tail.end(), tail.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (tail == kSuffix) {
            name.erase(name.size() - kSuffix.size());
        }
    }
    return name;
}

} // namespace gity::git

namespace gity::git {

std::string remoteUrlWithUser(std::string_view url, std::string_view username) {
    if (username.empty() || hostOfRemote(url).empty()) {
        return std::string(url);
    }

    const std::size_t scheme = url.find("://");
    if (scheme == std::string_view::npos) {
        // scp-style. Its "git@" is the ssh user; replacing it with an account
        // name would stop the remote resolving at all.
        return std::string(url);
    }

    const std::size_t hostStart = scheme + 3;
    const std::size_t path = url.find('/', hostStart);
    // The *last* '@' before the path ends the userinfo: a URL already holding
    // an unescaped email — the bug this replaced wrote exactly that — has two,
    // and the host is after the second. An '@' after the first '/' is path.
    const std::string_view authority =
        url.substr(hostStart, path == std::string_view::npos ? std::string_view::npos
                                                             : path - hostStart);
    const std::size_t atInAuthority = authority.rfind('@');
    const std::size_t hostBegins = atInAuthority == std::string_view::npos
                                       ? hostStart
                                       : hostStart + atInAuthority + 1;

    // Percent-encoded: an account name can be an email, and its '@' written
    // bare would split the address in the wrong place.
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    for (const char c : username) {
        const auto byte = static_cast<unsigned char>(c);
        if (std::isalnum(byte) != 0 || c == '-' || c == '.' || c == '_' || c == '~') {
            encoded += c;
        } else {
            encoded += '%';
            encoded += kHex[byte >> 4];
            encoded += kHex[byte & 0x0F];
        }
    }

    std::string result(url.substr(0, hostStart));
    result += encoded;
    result += '@';
    result += url.substr(hostBegins);
    return result;
}

} // namespace gity::git
