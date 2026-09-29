#include "Provider.h"

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

bool startsWith(const std::string& host, std::string_view prefix) {
    return host.rfind(prefix, 0) == 0;
}

} // namespace

std::vector<Provider> allProviders() {
    return {Provider::GitHub, Provider::GitLab, Provider::Bitbucket, Provider::Gitea,
            Provider::AzureDevOps, Provider::Other};
}

std::string_view providerName(Provider provider) {
    switch (provider) {
    case Provider::GitHub:
        return "GitHub";
    case Provider::GitLab:
        return "GitLab";
    case Provider::Bitbucket:
        return "Bitbucket";
    case Provider::Gitea:
        return "Gitea";
    case Provider::AzureDevOps:
        return "Azure DevOps";
    case Provider::Other:
        break;
    }
    return "Other";
}

std::string_view defaultHost(Provider provider) {
    switch (provider) {
    case Provider::GitHub:
        return "github.com";
    case Provider::GitLab:
        return "gitlab.com";
    case Provider::Bitbucket:
        return "bitbucket.org";
    case Provider::Gitea:
        return "gitea.com";
    case Provider::AzureDevOps:
        return "dev.azure.com";
    case Provider::Other:
        break;
    }
    return {};
}

Provider providerForHost(std::string_view host) {
    const std::string name = lowered(host);
    if (name.empty()) {
        return Provider::Other;
    }

    // The public instances, matched exactly. "github.com.evil.example" must
    // not be taken for GitHub, which a substring search would do.
    if (name == "github.com" || name == "www.github.com") {
        return Provider::GitHub;
    }
    if (name == "gitlab.com") {
        return Provider::GitLab;
    }
    if (name == "bitbucket.org") {
        return Provider::Bitbucket;
    }
    if (name == "gitea.com") {
        return Provider::Gitea;
    }
    if (name == "dev.azure.com" || name == "ssh.dev.azure.com" ||
        name.ends_with(".visualstudio.com")) {
        return Provider::AzureDevOps;
    }

    // A host that begins with a public instance and keeps going is pretending
    // to be it: "github.com.evil.example" is a subdomain of evil.example, and
    // the prefix rule below would otherwise label it GitHub. Caught before the
    // self-hosted guess rather than after, because a trusted name on an
    // untrusted host is the whole point of the trick.
    for (const Provider candidate : allProviders()) {
        const std::string_view instance = defaultHost(candidate);
        if (!instance.empty() && startsWith(name, std::string(instance) + ".")) {
            return Provider::Other;
        }
    }

    // Self-hosted, and only where the name says which. A studio running
    // "git.example.com" could be any of these, and picking one would put a
    // confident wrong label on the account.
    if (startsWith(name, "github.")) {
        return Provider::GitHub;
    }
    if (startsWith(name, "gitlab.")) {
        return Provider::GitLab;
    }
    if (startsWith(name, "gitea.")) {
        return Provider::Gitea;
    }
    return Provider::Other;
}

std::string hostOfRemote(std::string_view url) {
    std::string text(url);
    // Trim whitespace from a pasted URL.
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.pop_back();
    }
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }
    text = text.substr(start);
    if (text.empty()) {
        return {};
    }

    // A scheme, if there is one. file:// has no host worth reporting.
    const std::size_t scheme = text.find("://");
    if (scheme != std::string::npos) {
        const std::string protocol = lowered(text.substr(0, scheme));
        if (protocol == "file") {
            return {};
        }
        text = text.substr(scheme + 3);
    } else if (text.front() == '/' || text.front() == '.' ||
               (text.size() > 1 && text[1] == ':')) {
        // A local path — absolute, relative, or a Windows drive letter.
        return {};
    }

    // Strip any userinfo: "git@host" and "user:token@host" both appear. The
    // last '@' before the path, so an unescaped email user ("a@b.io@host")
    // does not make its domain the host.
    const std::size_t at = text.substr(0, text.find('/')).rfind('@');
    if (at != std::string::npos) {
        text = text.substr(at + 1);
    }

    // The host ends at the first '/' (URL) or ':' (scp-style, or a port).
    const std::size_t end = text.find_first_of(":/");
    if (end != std::string::npos) {
        text = text.substr(0, end);
    }
    return lowered(text);
}

} // namespace gity::git
