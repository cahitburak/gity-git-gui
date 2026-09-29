#include "Submodules.h"

#include <algorithm>

namespace gity::git {
namespace {

struct Collector {
    std::vector<SubmoduleEntry>* out;
    git_repository* repo;
};

int collect(git_submodule* submodule, const char* name, void* payload) {
    auto* collector = static_cast<Collector*>(payload);

    SubmoduleEntry entry;
    entry.name = name != nullptr ? name : "";
    if (const char* path = git_submodule_path(submodule)) {
        entry.path = path;
    }
    if (const char* url = git_submodule_url(submodule)) {
        entry.url = url;
    }

    if (const git_oid* recorded = git_submodule_head_id(submodule)) {
        char buffer[GIT_OID_SHA1_HEXSIZE + 1] = {};
        git_oid_tostr(buffer, sizeof(buffer), recorded);
        entry.recordedShortId = std::string(buffer).substr(0, 8);
    }

    unsigned int status = 0;
    if (git_submodule_status(&status, collector->repo, entry.name.c_str(),
                             GIT_SUBMODULE_IGNORE_UNSPECIFIED) == 0) {
        entry.initialised = (status & GIT_SUBMODULE_STATUS_IN_WD) != 0;
        entry.movedAway = (status & GIT_SUBMODULE_STATUS_WD_MODIFIED) != 0;
        // "Dirty" covers all three ways a submodule's own working copy can
        // differ; they mean the same thing to someone looking at the sidebar.
        entry.dirty = (status & (GIT_SUBMODULE_STATUS_WD_INDEX_MODIFIED |
                                 GIT_SUBMODULE_STATUS_WD_WD_MODIFIED |
                                 GIT_SUBMODULE_STATUS_WD_UNTRACKED)) != 0;
    }

    collector->out->push_back(std::move(entry));
    return 0;
}

} // namespace

std::vector<SubmoduleEntry> listSubmodules(git_repository* repo) {
    std::vector<SubmoduleEntry> submodules;
    Collector collector{&submodules, repo};
    git_submodule_foreach(repo, collect, &collector);

    std::sort(submodules.begin(), submodules.end(),
              [](const SubmoduleEntry& a, const SubmoduleEntry& b) { return a.path < b.path; });
    return submodules;
}

} // namespace gity::git
