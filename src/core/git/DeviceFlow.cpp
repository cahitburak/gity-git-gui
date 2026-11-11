#include "DeviceFlow.h"

#include <algorithm>
#include <cctype>

namespace gity::git {
namespace {

int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/// application/x-www-form-urlencoded: '+' is a space and %XX a byte. GitHub's
/// form reply encodes the verification URL as https%3A%2F%2F…
std::string formDecode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '+') {
            out += ' ';
        } else if (text[i] == '%' && i + 2 < text.size() && hexValue(text[i + 1]) >= 0 &&
                   hexValue(text[i + 2]) >= 0) {
            out += static_cast<char>(hexValue(text[i + 1]) * 16 + hexValue(text[i + 2]));
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

/// A value from either JSON or form encoding.
///
/// Both are needed: GitHub answers the device endpoint with
/// `application/x-www-form-urlencoded` unless asked for JSON, and returns JSON
/// elsewhere. Accepting both means the flow does not break on a provider that
/// ignores the Accept header — which GitHub historically did.
std::string valueOf(std::string_view body, std::string_view key) {
    // JSON: "key":"value" or "key":123
    std::string quoted = "\"";
    quoted += key;
    quoted += "\"";
    std::size_t at = body.find(quoted);
    if (at != std::string_view::npos) {
        at = body.find(':', at + quoted.size());
        if (at == std::string_view::npos) {
            return {};
        }
        ++at;
        while (at < body.size() && (body[at] == ' ' || body[at] == '\t')) {
            ++at;
        }
        if (at < body.size() && body[at] == '"') {
            // A JSON string, escapes decoded: a provider may write the
            // verification URL as "https:\/\/github.com\/login\/device".
            std::string value;
            for (std::size_t i = at + 1; i < body.size(); ++i) {
                const char c = body[i];
                if (c == '"') {
                    return value;
                }
                if (c == '\\' && i + 1 < body.size()) {
                    const char escaped = body[++i];
                    switch (escaped) {
                    case 'n':
                        value += '\n';
                        break;
                    case 't':
                        value += '\t';
                        break;
                    case 'r':
                        value += '\r';
                        break;
                    default:
                        value += escaped; // \" \\ \/ and anything else, literally
                        break;
                    }
                    continue;
                }
                value += c;
            }
            return {}; // unterminated
        }
        const std::size_t end = body.find_first_of(",}", at);
        std::string number(body.substr(at, end == std::string_view::npos ? end : end - at));
        while (!number.empty() && std::isspace(static_cast<unsigned char>(number.back())) != 0) {
            number.pop_back();
        }
        // JSON's null is no value, not the four letters: an error reply with
        // "access_token": null would otherwise be stored as the token "null".
        if (number == "null") {
            return {};
        }
        return number;
    }

    // Form encoding: key=value&…
    std::size_t search = 0;
    while ((at = body.find(key, search)) != std::string_view::npos) {
        const bool atStart = at == 0 || body[at - 1] == '&' || body[at - 1] == '?';
        const std::size_t after = at + key.size();
        if (atStart && after < body.size() && body[after] == '=') {
            const std::size_t end = body.find('&', after + 1);
            return formDecode(
                body.substr(after + 1, end == std::string_view::npos ? end : end - after - 1));
        }
        search = at + 1;
    }
    return {};
}

int intValue(std::string_view body, std::string_view key, int fallback) {
    const std::string text = valueOf(body, key);
    if (text.empty()) {
        return fallback;
    }
    try {
        return std::stoi(text);
    } catch (...) {
        return fallback;
    }
}

} // namespace

DeviceFlowEndpoints endpointsFor(Provider provider, std::string_view host) {
    const std::string base(host.empty() ? defaultHost(provider) : host);
    if (base.empty()) {
        return {};
    }

    DeviceFlowEndpoints endpoints;
    switch (provider) {
    case Provider::GitHub:
        // github.com serves the flow from its own domain; GitHub Enterprise
        // serves it from the instance.
        endpoints.codeUrl = "https://" + base + "/login/device/code";
        endpoints.tokenUrl = "https://" + base + "/login/oauth/access_token";
        endpoints.scope = "repo";
        break;
    case Provider::GitLab:
        endpoints.codeUrl = "https://" + base + "/oauth/authorize_device";
        endpoints.tokenUrl = "https://" + base + "/oauth/token";
        endpoints.scope = "read_repository write_repository";
        break;
    case Provider::Gitea:
        endpoints.codeUrl = "https://" + base + "/login/oauth/authorize_device";
        endpoints.tokenUrl = "https://" + base + "/login/oauth/access_token";
        endpoints.scope = "write:repository";
        break;
    case Provider::Bitbucket:
    case Provider::AzureDevOps:
    case Provider::Other:
        // No device grant, or not one this client knows. Saying so beats
        // guessing an endpoint and failing at the request.
        break;
    }
    return endpoints;
}

DeviceCode parseDeviceCode(std::string_view body) {
    DeviceCode code;
    code.deviceCode = valueOf(body, "device_code");
    code.userCode = valueOf(body, "user_code");
    code.verificationUrl = valueOf(body, "verification_uri");
    if (code.verificationUrl.empty()) {
        // GitLab and some others spell it without the underscore-free form.
        code.verificationUrl = valueOf(body, "verification_url");
    }
    code.expiresInSeconds = intValue(body, "expires_in", 900);
    // Bounded at both ends. Zero would poll as fast as the network allows and
    // be rate-limited instantly; an absurdly large value — from a broken or
    // hostile response — overflows when multiplied out to milliseconds, and a
    // negative timer interval fires immediately, turning a slow poll into a
    // tight loop. RFC 8628 intervals are single digits, so a minute is already
    // far past anything legitimate.
    code.intervalSeconds = std::clamp(intValue(body, "interval", 5), 1, 60);
    return code;
}

PollResult parsePollResponse(std::string_view body) {
    PollResult result;

    const std::string token = valueOf(body, "access_token");
    if (!token.empty()) {
        result.state = PollState::Granted;
        result.accessToken = token;
        return result;
    }

    const std::string error = valueOf(body, "error");
    if (error == "authorization_pending") {
        result.state = PollState::Pending;
    } else if (error == "slow_down") {
        result.state = PollState::SlowDown;
    } else if (error == "access_denied") {
        result.state = PollState::Denied;
    } else if (error == "expired_token") {
        result.state = PollState::Expired;
    } else {
        result.state = PollState::Failed;
    }

    result.detail = valueOf(body, "error_description");
    if (result.detail.empty()) {
        result.detail = error;
    }
    return result;
}

} // namespace gity::git
