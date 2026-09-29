#include "UndoPlan.h"

#include <algorithm>
#include <set>

namespace gity::git {
namespace {

std::string valueOf(const RefMap& refs, const std::string& name) {
    const auto found = refs.find(name);
    return found == refs.end() ? std::string() : found->second;
}

bool contains(const std::vector<StashEntry>& list, const std::string& oid) {
    return std::any_of(list.begin(), list.end(),
                       [&oid](const StashEntry& entry) { return entry.oid == oid; });
}

} // namespace

RefRestorePlan planRefRestore(const RefMap& before, const RefMap& after,
                              const RefMap& current) {
    std::set<std::string> names;
    for (const auto& [name, oid] : before) {
        names.insert(name);
    }
    for (const auto& [name, oid] : after) {
        names.insert(name);
    }

    RefRestorePlan plan;
    for (const std::string& name : names) {
        const std::string was = valueOf(before, name);
        const std::string left = valueOf(after, name);
        if (was == left) {
            continue; // the operation did not touch it
        }
        const std::string now = valueOf(current, name);
        if (now != left) {
            if (now != was) {
                plan.skipped.push_back(name);
            }
            // now == was: already back where it belongs; nothing to do.
            continue;
        }
        plan.changes.push_back(RefChange{name, now, was});
    }
    return plan;
}

StashRestorePlan planStashRestore(const std::vector<StashEntry>& before,
                                  const std::vector<StashEntry>& after,
                                  const std::vector<StashEntry>& current) {
    StashRestorePlan plan;
    // Oldest first, so storing them back leaves them in their old order.
    for (auto entry = before.rbegin(); entry != before.rend(); ++entry) {
        if (!contains(after, entry->oid) && !contains(current, entry->oid)) {
            plan.store.push_back(*entry);
        }
    }
    for (const StashEntry& entry : after) {
        if (!contains(before, entry.oid) && contains(current, entry.oid)) {
            plan.drop.push_back(entry.oid);
        }
    }
    return plan;
}

} // namespace gity::git
