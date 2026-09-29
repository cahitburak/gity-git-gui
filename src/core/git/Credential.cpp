#include "Credential.h"

#include <algorithm>

namespace gity::git {
namespace {

bool clean(const std::string& value) {
    return value.find_first_of(std::string_view("\n\r\0", 3)) == std::string::npos;
}

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    });
    return out;
}

} // namespace

bool credentialForUrl(std::string_view url, bool useHttpPath, Credential* out) {
    const auto scheme = url.find("://");
    if (scheme == std::string_view::npos) {
        return false; // scp-style ssh, or a local path
    }
    const std::string protocol = lower(url.substr(0, scheme));
    if (protocol != "https" && protocol != "http") {
        return false; // ssh:// and file:// authenticate some other way
    }

    std::string_view rest = url.substr(scheme + 3);
    const auto slash = rest.find('/');
    std::string_view authority = rest.substr(0, slash);
    std::string_view path = slash == std::string_view::npos ? std::string_view()
                                                            : rest.substr(slash + 1);

    Credential credential;
    credential.protocol = protocol;
    if (const auto at = authority.rfind('@'); at != std::string_view::npos) {
        std::string_view userinfo = authority.substr(0, at);
        // A password in the URL is not carried: git would use it without
        // asking the helper, and showing it here would be showing a secret
        // that lives in a config file rather than in the helper.
        credential.username = std::string(userinfo.substr(0, userinfo.find(':')));
        authority = authority.substr(at + 1);
    }
    if (authority.empty()) {
        return false;
    }
    credential.host = lower(authority);
    if (useHttpPath) {
        credential.path = std::string(path);
    }
    if (out != nullptr) {
        *out = std::move(credential);
    }
    return true;
}

Credential parseCredential(std::string_view text) {
    Credential credential;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        start = end + 1;
        if (line.empty()) {
            break; // the blank line ends the record
        }
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }
        const std::string_view key = line.substr(0, equals);
        const std::string value(line.substr(equals + 1));
        if (key == "protocol") {
            credential.protocol = value;
        } else if (key == "host") {
            credential.host = value;
        } else if (key == "path") {
            credential.path = value;
        } else if (key == "username") {
            credential.username = value;
        } else if (key == "password") {
            credential.password = value;
        }
    }
    return credential;
}

std::string serializeCredential(const Credential& credential) {
    std::string out;
    const auto field = [&out](std::string_view key, const std::string& value) {
        if (!value.empty()) {
            out.append(key).append("=").append(value).append("\n");
        }
    };
    field("protocol", credential.protocol);
    field("host", credential.host);
    field("path", credential.path);
    field("username", credential.username);
    field("password", credential.password);
    out += '\n';
    return out;
}

bool isWritable(const Credential& credential) {
    return clean(credential.protocol) && clean(credential.host) && clean(credential.path) &&
           clean(credential.username) && clean(credential.password);
}

} // namespace gity::git
