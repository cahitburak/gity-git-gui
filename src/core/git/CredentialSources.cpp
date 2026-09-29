#include "CredentialSources.h"

#include <cctype>

namespace gity::git {
namespace {

std::string percentDecode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && std::isxdigit(static_cast<unsigned char>(text[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            out += static_cast<char>(std::stoi(std::string(text.substr(i + 1, 2)), nullptr, 16));
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

std::vector<std::string_view> lines(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        out.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

} // namespace

std::vector<StoredEntry> parseCredentialStore(std::string_view text) {
    std::vector<StoredEntry> entries;
    for (std::string_view line : lines(text)) {
        line = trimmed(line);
        const auto scheme = line.find("://");
        if (line.empty() || scheme == std::string_view::npos) {
            continue;
        }
        StoredEntry entry;
        entry.protocol = std::string(line.substr(0, scheme));
        std::string_view rest = line.substr(scheme + 3);
        const auto slash = rest.find('/');
        std::string_view authority = rest.substr(0, slash);
        if (slash != std::string_view::npos) {
            entry.path = percentDecode(rest.substr(slash + 1));
        }
        if (const auto at = authority.rfind('@'); at != std::string_view::npos) {
            const std::string_view userinfo = authority.substr(0, at);
            const auto colon = userinfo.find(':');
            entry.username = percentDecode(userinfo.substr(0, colon));
            entry.hasPassword = colon != std::string_view::npos && colon + 1 < userinfo.size();
            authority = authority.substr(at + 1);
        }
        entry.host = percentDecode(authority);
        if (!entry.host.empty()) {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

AccountMismatch findAccountMismatch(std::string_view remoteUrl,
                                    const std::vector<GhAccount>& accounts) {
    AccountMismatch mismatch;
    const auto scheme = remoteUrl.find("://");
    if (scheme == std::string_view::npos) {
        return mismatch;
    }
    std::string_view authority = remoteUrl.substr(scheme + 3);
    authority = authority.substr(0, authority.find('/'));
    const auto at = authority.rfind('@');
    if (at == std::string_view::npos) {
        return mismatch; // no user named: GitHub CLI answers with its active one
    }
    const std::string_view userinfo = authority.substr(0, at);
    const std::string user = percentDecode(userinfo.substr(0, userinfo.find(':')));
    std::string host(authority.substr(at + 1));
    for (char& c : host) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    bool known = false;
    bool hostKnown = false;
    for (const GhAccount& account : accounts) {
        if (account.host != host) {
            continue;
        }
        hostKnown = true;
        if (account.active) {
            mismatch.activeAccount = account.account;
        }
        known = known || account.account == user;
    }
    if (!hostKnown || user == mismatch.activeAccount) {
        return mismatch;
    }
    mismatch.urlUser = user;
    mismatch.kind = known ? AccountMismatch::Kind::InactiveUser : AccountMismatch::Kind::UnknownUser;
    return mismatch;
}

std::vector<GhAccount> parseGhAuthStatus(std::string_view text) {
    // A host on its own line, then per account:
    //   ✓ Logged in to github.com account NAME (keyring)
    //   - Active account: true
    std::vector<GhAccount> accounts;
    static constexpr std::string_view kLoggedIn = "Logged in to ";
    static constexpr std::string_view kAccount = " account ";
    static constexpr std::string_view kActive = "Active account:";
    for (std::string_view line : lines(text)) {
        line = trimmed(line);
        if (const auto at = line.find(kLoggedIn); at != std::string_view::npos) {
            std::string_view rest = line.substr(at + kLoggedIn.size());
            const auto account = rest.find(kAccount);
            if (account == std::string_view::npos) {
                continue;
            }
            GhAccount entry;
            entry.host = std::string(rest.substr(0, account));
            rest = rest.substr(account + kAccount.size());
            const auto paren = rest.find(" (");
            entry.account = std::string(rest.substr(0, paren));
            if (paren != std::string_view::npos) {
                std::string_view storage = rest.substr(paren + 2);
                if (!storage.empty() && storage.back() == ')') {
                    storage.remove_suffix(1);
                }
                entry.storage = std::string(storage);
            }
            accounts.push_back(std::move(entry));
        } else if (const auto active = line.find(kActive);
                   active != std::string_view::npos && !accounts.empty()) {
            accounts.back().active = trimmed(line.substr(active + kActive.size())) == "true";
        }
    }
    return accounts;
}

} // namespace gity::git
