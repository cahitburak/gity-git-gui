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
    const std::string_view authority = url.substr(
        hostStart, path == std::string_view::npos ? std::string_view::npos : path - hostStart);
    const std::size_t atInAuthority = authority.rfind('@');
    const std::size_t hostBegins =
        atInAuthority == std::string_view::npos ? hostStart : hostStart + atInAuthority + 1;

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
        "ghp_",  "gho_",   "ghu_", "ghs_",  "ghr_",  "github_pat_", "glpat-",
        "gldt-", "glptt-", "ATBB", "ATCTT", "xoxb-", "xoxp-",
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

/// The userinfo split at its first ':', as git splits it: a password may hold
/// a colon and a username may not.
struct UserPassword {
    std::string_view user;
    std::string_view password;
    bool hasPassword = false;
};

UserPassword splitUserinfo(std::string_view userinfo) {
    const std::size_t colon = userinfo.find(':');
    if (colon == std::string_view::npos) {
        return {userinfo, {}, false};
    }
    return {userinfo.substr(0, colon), userinfo.substr(colon + 1), true};
}

bool equalFolded(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

/// The scheme of the URL whose userinfo starts at `info.begin`: the word
/// before its "://".
std::string_view schemeBefore(std::string_view text, const Userinfo& info) {
    const std::size_t end = info.begin - 3;
    std::size_t begin = end;
    while (begin > 0 &&
           (std::isalnum(static_cast<unsigned char>(text[begin - 1])) != 0 ||
            text[begin - 1] == '+' || text[begin - 1] == '-' || text[begin - 1] == '.')) {
        --begin;
    }
    return text.substr(begin, end - begin);
}

/// The host, with any port, after the userinfo's '@'.
std::string_view hostAfter(std::string_view text, const Userinfo& info) {
    std::size_t end = info.end + 1;
    while (end < text.size() && !endsAuthority(text[end])) {
        ++end;
    }
    return text.substr(info.end + 1, end - info.end - 1);
}

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
        const UserPassword parts = splitUserinfo(text.substr(info.begin, info.end - info.begin));
        out += text.substr(copied, info.begin - copied);
        // The username is looked at even when a password follows it: a token
        // is also written as the user with a filler password
        // (`ghp_xxxx:x-oauth-basic@`), or with an empty one (`ghp_xxxx:@`).
        out += looksLikeToken(parts.user) ? kMask : parts.user;
        if (parts.hasPassword) {
            out += ':';
            out += kMask;
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
    const UserPassword old = splitUserinfo(original.substr(was.begin, was.end - was.begin));
    const UserPassword typed = splitUserinfo(edited.substr(now.begin, now.end - now.begin));

    // A part is only put back where masking took one out: a masked username
    // was a token, and a masked password was whatever the original had.
    const bool userMasked = typed.user == kMask && looksLikeToken(old.user);
    const bool passwordMasked = typed.hasPassword && typed.password == kMask && old.hasPassword;
    if (!userMasked && !passwordMasked) {
        return std::string(edited); // the person typed their own
    }

    // The secret belongs to the server it was written for. An edit that points
    // the remote at another host, or downgrades it to plain http, would hand
    // the token to that server: the masked parts are dropped instead, so
    // neither the secret nor a literal "***" is saved.
    const std::string_view oldScheme = schemeBefore(original, was);
    const std::string_view newScheme = schemeBefore(edited, now);
    const bool sameScheme = equalFolded(oldScheme, newScheme) ||
                            (equalFolded(oldScheme, "http") && equalFolded(newScheme, "https"));
    const bool restore =
        sameScheme && equalFolded(hostAfter(original, was), hostAfter(edited, now));

    std::string userinfo;
    if (!userMasked) {
        userinfo = typed.user;
    } else if (restore) {
        userinfo = old.user;
    }
    if (typed.hasPassword && (!passwordMasked || restore)) {
        userinfo += ':';
        userinfo += passwordMasked ? old.password : typed.password;
    }

    std::string out(edited.substr(0, now.begin));
    if (userinfo.empty()) {
        out += edited.substr(now.end + 1); // nothing left before the '@', so no '@'
    } else {
        out += userinfo;
        out += edited.substr(now.end);
    }
    return out;
}

} // namespace gity::git
