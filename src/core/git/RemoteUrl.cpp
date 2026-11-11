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
                            url.back() == '/' || url.back() == '\\')) {
        url.remove_suffix(1);
    }
    if (url.empty()) {
        return {};
    }

    // scp-style remotes (git@host:owner/repo.git) have no scheme, so the last
    // segment starts after ':' as well as after '/'. Taking the later of the
    // two handles both, and handles a colon inside an https URL's port because
    // a path separator always follows it.
    // A backslash separates too: a Windows path to a local repository,
    // C:\src\proj.git, is a remote like any other.
    const std::size_t slash = url.find_last_of("/\\");
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

namespace {

/// Where a URL's userinfo lies within `text`, starting the search at `from`:
/// the characters between "://" and the '@' that ends the authority.
struct Userinfo {
    std::size_t begin = std::string_view::npos;
    std::size_t end = 0; ///< The '@'.
};

bool endsAuthority(char c) noexcept {
    return c == '/' || c == '?' || c == '#' || c == '\'' || c == '"' || c == '<' || c == '>' ||
           std::isspace(static_cast<unsigned char>(c)) != 0;
}

Userinfo findUserinfo(std::string_view text, std::size_t from) {
    while (true) {
        const std::size_t scheme = text.find("://", from);
        if (scheme == std::string_view::npos) {
            return {};
        }
        const std::size_t begin = scheme + 3;
        std::size_t at = std::string_view::npos;
        for (std::size_t i = begin; i < text.size() && !endsAuthority(text[i]); ++i) {
            if (text[i] == '@') {
                at = i; // the last '@' before the path ends the userinfo
            }
        }
        if (at != std::string_view::npos && at > begin) {
            return {begin, at};
        }
        from = begin;
    }
}

/// A username that is really a token. Forges document cloning with the token
/// as the whole userinfo, and their tokens have recognisable prefixes; past
/// that, a long run of token characters is no one's name.
bool looksLikeToken(std::string_view user) {
    static constexpr std::string_view kPrefixes[] = {
        "ghp_", "gho_", "ghu_", "ghs_", "ghr_", "github_pat_", "glpat-", "gldt-", "glptt-",
        "ATBB", "ATCTT", "xoxb-", "xoxp-",
    };
    for (const std::string_view prefix : kPrefixes) {
        if (user.substr(0, prefix.size()) == prefix) {
            return true;
        }
    }
    if (user.size() < 32) {
        return false;
    }
    for (const char c : user) {
        const auto byte = static_cast<unsigned char>(c);
        if (std::isalnum(byte) == 0 && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}

constexpr std::string_view kMask = "***";

} // namespace

std::string maskUrlCredentials(std::string_view text) {
    std::string out;
    std::size_t copied = 0;
    std::size_t from = 0;
    while (true) {
        const Userinfo info = findUserinfo(text, from);
        if (info.begin == std::string_view::npos) {
            break;
        }
        const std::string_view userinfo = text.substr(info.begin, info.end - info.begin);
        const std::size_t colon = userinfo.find(':');
        out += text.substr(copied, info.begin - copied);
        if (colon != std::string_view::npos) {
            out += userinfo.substr(0, colon);
            out += ':';
            out += kMask;
        } else if (looksLikeToken(userinfo)) {
            out += kMask;
        } else {
            out += userinfo;
        }
        copied = info.end;
        from = info.end;
    }
    out += text.substr(copied);
    return out;
}

std::string unmaskUrlCredentials(std::string_view edited, std::string_view original) {
    const Userinfo was = findUserinfo(original, 0);
    const Userinfo now = findUserinfo(edited, 0);
    if (was.begin == std::string_view::npos || now.begin == std::string_view::npos) {
        return std::string(edited);
    }
    const std::string_view oldInfo = original.substr(was.begin, was.end - was.begin);
    const std::string_view newInfo = edited.substr(now.begin, now.end - now.begin);
    const std::size_t oldColon = oldInfo.find(':');
    const std::size_t newColon = newInfo.find(':');

    std::string restored;
    if (newInfo == kMask && oldColon == std::string_view::npos) {
        restored = std::string(oldInfo); // the token that was the username
    } else if (newColon != std::string_view::npos && newInfo.substr(newColon + 1) == kMask &&
               oldColon != std::string_view::npos) {
        restored = std::string(newInfo.substr(0, newColon + 1));
        restored += oldInfo.substr(oldColon + 1);
    } else {
        return std::string(edited); // the person typed their own
    }

    std::string out(edited.substr(0, now.begin));
    out += restored;
    out += edited.substr(now.end);
    return out;
}

} // namespace gity::git
