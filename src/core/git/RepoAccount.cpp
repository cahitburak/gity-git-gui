#include "RepoAccount.h"

#include <cctype>

namespace gity::git {
namespace {

constexpr std::string_view kMarker = "gh auth token --hostname ";
constexpr std::string_view kUser = " --user ";

bool safeHost(std::string_view host) {
    if (host.empty()) {
        return false;
    }
    for (const char c : host) {
        const auto byte = static_cast<unsigned char>(c);
        if (std::isalnum(byte) == 0 && c != '.' && c != '-' && c != ':') {
            return false;
        }
    }
    return true;
}

} // namespace

bool isSafeAccountName(std::string_view name) {
    if (name.empty() || name.size() > 100) {
        return false;
    }
    for (const char c : name) {
        const auto byte = static_cast<unsigned char>(c);
        if (std::isalnum(byte) == 0 && c != '-' && c != '_') {
            return false;
        }
    }
    return true;
}

std::string ghAccountHelper(std::string_view host, std::string_view account) {
    if (!isSafeAccountName(account) || !safeHost(host)) {
        return {};
    }
    // Answers only `get`: store and erase are gh's business, and git calls
    // them on every success and failure. The name is echoed as the username so
    // git and the server agree on whose token this is.
    std::string helper = "!f() { test \"$1\" = get || exit 0; echo username=";
    helper += account;
    helper += "; echo \"password=$(";
    helper += kMarker;
    helper += host;
    helper += kUser;
    helper += account;
    helper += ")\"; }; f";
    return helper;
}

std::string accountOfGhHelper(std::string_view helper) {
    const auto marker = helper.find(kMarker);
    if (marker == std::string_view::npos) {
        return {};
    }
    const auto user = helper.find(kUser, marker);
    if (user == std::string_view::npos) {
        return {};
    }
    std::size_t start = user + kUser.size();
    std::size_t end = start;
    while (end < helper.size() &&
           (std::isalnum(static_cast<unsigned char>(helper[end])) != 0 || helper[end] == '-' ||
            helper[end] == '_')) {
        ++end;
    }
    return std::string(helper.substr(start, end - start));
}

} // namespace gity::git
