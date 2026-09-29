#include "Remotes.h"

#include <algorithm>
#include <cctype>

namespace gity::git {

std::vector<RemoteEntry> listRemotes(git_repository* repo) {
    std::vector<RemoteEntry> remotes;
    git_strarray names = {};
    if (git_remote_list(&names, repo) != 0) {
        return remotes;
    }

    for (std::size_t i = 0; i < names.count; ++i) {
        git_remote* rawRemote = nullptr;
        if (git_remote_lookup(&rawRemote, repo, names.strings[i]) != 0) {
            continue;
        }
        const Handle<git_remote, git_remote_free> remote(rawRemote);

        RemoteEntry entry;
        entry.name = names.strings[i];
        if (const char* url = git_remote_url(remote.get())) {
            entry.fetchUrl = url;
        }
        // Absent unless the user set one; then it is the URL that actually
        // receives their work, which makes it worth showing.
        entry.pushUrl = git_remote_pushurl(remote.get()) != nullptr
                            ? git_remote_pushurl(remote.get())
                            : entry.fetchUrl;
        remotes.push_back(std::move(entry));
    }
    git_strarray_dispose(&names);

    std::sort(remotes.begin(), remotes.end(), [](const RemoteEntry& a, const RemoteEntry& b) {
        // origin first: it is the one nearly every action means.
        if ((a.name == "origin") != (b.name == "origin")) {
            return a.name == "origin";
        }
        return a.name < b.name;
    });
    return remotes;
}

RemoteNameProblem checkRemoteName(std::string_view name,
                                  const std::vector<RemoteEntry>& existing) {
    if (name.empty()) {
        return RemoteNameProblem::Empty;
    }
    for (const char c : name) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            return RemoteNameProblem::Whitespace;
        }
    }

    // The characters git forbids in a ref component. A remote becomes
    // refs/remotes/<name>/…, so these are forbidden here for the same reason.
    constexpr std::string_view kIllegal = "~^:?*[\\\177";
    for (const char c : name) {
        if (kIllegal.find(c) != std::string_view::npos ||
            static_cast<unsigned char>(c) < 0x20) {
            return RemoteNameProblem::IllegalCharacter;
        }
    }
    if (name.find("..") != std::string_view::npos ||
        name.find("@{") != std::string_view::npos) {
        return RemoteNameProblem::IllegalCharacter;
    }
    if (name.front() == '.' || name.back() == '.') {
        return RemoteNameProblem::LeadingOrTrailingDot;
    }

    const bool taken = std::any_of(existing.begin(), existing.end(),
                                   [&](const RemoteEntry& e) { return e.name == name; });
    return taken ? RemoteNameProblem::AlreadyExists : RemoteNameProblem::None;
}

} // namespace gity::git
