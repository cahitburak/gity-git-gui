#include "PullRequest.h"

#include <cctype>

namespace gity::git {
namespace {

bool unreserved(unsigned char c) {
    return std::isalnum(c) != 0 || c == '-' || c == '.' || c == '_' || c == '~';
}

/// Percent-encodes everything but the unreserved characters — and '/', when
/// the name goes into a path, where GitHub and Gitea read it as part of the
/// branch name.
std::string encode(std::string_view text, bool keepSlash) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (unreserved(c) || (keepSlash && c == '/')) {
            out += ch;
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0x0F];
        }
    }
    return out;
}

std::string trimmed(std::string_view text) {
    std::size_t start = 0;
    std::size_t end = text.size();
    while (start < end && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(start, end - start));
}

} // namespace

std::string repositoryWebUrl(std::string_view remoteUrl) {
    std::string text = trimmed(remoteUrl);
    if (text.empty()) {
        return {};
    }

    bool https = false;
    const std::size_t scheme = text.find("://");
    if (scheme != std::string::npos) {
        std::string protocol = text.substr(0, scheme);
        for (char& c : protocol) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (protocol == "file") {
            return {};
        }
        https = protocol == "https" || protocol == "http";
        text = text.substr(scheme + 3);
    } else if (text.front() == '/' || text.front() == '.' ||
               (text.size() > 1 && text[1] == ':')) {
        return {}; // a local path
    }

    // Userinfo, up to the last '@' before the path (see hostOfRemote).
    const std::size_t at = text.substr(0, text.find('/')).rfind('@');
    if (at != std::string::npos) {
        text = text.substr(at + 1);
    }

    std::string host;
    std::string port;
    std::string path;
    if (scheme != std::string::npos) {
        const std::size_t slash = text.find('/');
        std::string authority = slash == std::string::npos ? text : text.substr(0, slash);
        path = slash == std::string::npos ? std::string() : text.substr(slash + 1);
        const std::size_t colon = authority.find(':');
        if (colon != std::string::npos) {
            port = authority.substr(colon + 1);
            authority = authority.substr(0, colon);
        }
        host = authority;
    } else {
        // scp-style: host:path
        const std::size_t colon = text.find(':');
        if (colon == std::string::npos) {
            return {};
        }
        host = text.substr(0, colon);
        path = text.substr(colon + 1);
    }
    for (char& c : host) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    while (!path.empty() && path.front() == '/') {
        path.erase(0, 1);
    }
    while (!path.empty() && path.back() == '/') {
        path.pop_back();
    }
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".git") == 0) {
        path.resize(path.size() - 4);
    }
    if (host.empty() || path.empty()) {
        return {};
    }

    // Azure DevOps over ssh: ssh.dev.azure.com:v3/org/project/repo is
    // dev.azure.com/org/project/_git/repo on the web.
    if (host == "ssh.dev.azure.com" && path.rfind("v3/", 0) == 0) {
        const std::string rest = path.substr(3);
        const std::size_t last = rest.rfind('/');
        if (last == std::string::npos) {
            return {};
        }
        return "https://dev.azure.com/" + rest.substr(0, last) + "/_git/" + rest.substr(last + 1);
    }

    std::string url = "https://" + host;
    if (https && !port.empty()) {
        url += ":" + port;
    }
    return url + "/" + path;
}

std::optional<std::string> newPullRequestUrl(Provider provider, std::string_view remoteUrl,
                                             std::string_view source, std::string_view target) {
    const std::string base = repositoryWebUrl(remoteUrl);
    if (base.empty() || source.empty()) {
        return std::nullopt;
    }
    switch (provider) {
    case Provider::GitHub:
        return base + "/compare/" +
               (target.empty() ? std::string() : encode(target, true) + "...") +
               encode(source, true) + "?expand=1";
    case Provider::Gitea:
        return base + "/compare/" +
               (target.empty() ? std::string() : encode(target, true) + "...") +
               encode(source, true);
    case Provider::GitLab:
        return base + "/-/merge_requests/new?merge_request%5Bsource_branch%5D=" +
               encode(source, false) +
               (target.empty() ? std::string()
                               : "&merge_request%5Btarget_branch%5D=" + encode(target, false));
    case Provider::Bitbucket:
        return base + "/pull-requests/new?source=" + encode(source, false) +
               (target.empty() ? std::string() : "&dest=" + encode(target, false));
    case Provider::AzureDevOps:
        return base + "/pullrequestcreate?sourceRef=" + encode(source, false) +
               (target.empty() ? std::string() : "&targetRef=" + encode(target, false));
    case Provider::Other:
        break;
    }
    return std::nullopt;
}

std::string_view pullRequestNoun(Provider provider) {
    return provider == Provider::GitLab ? "merge request" : "pull request";
}

} // namespace gity::git
