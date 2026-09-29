#include "CommitStore.h"

#include <algorithm>
#include <cstring>

namespace gity::model {

StringArena::Ref StringArena::add(std::string_view text) {
    // A summary longer than this is a pasted stack trace, not a summary.
    constexpr std::size_t kMaxLength = 8192;
    const std::size_t length = std::min(text.size(), kMaxLength);

    Ref ref;
    ref.offset = static_cast<std::uint32_t>(data_.size());
    ref.length = static_cast<std::uint32_t>(length);
    data_.insert(data_.end(), text.begin(), text.begin() + static_cast<std::ptrdiff_t>(length));
    return ref;
}

void CommitStore::reserve(std::size_t commits) {
    oids_.reserve(commits);
    meta_.reserve(commits);
    strings_.reserve(commits * 64); // rough: summary + author per commit
}

CommitIndex CommitStore::append(const git_oid& oid, std::string_view summary,
                                std::string_view author, std::int64_t when) {
    const auto index = static_cast<CommitIndex>(oids_.size());
    oids_.push_back(oid);
    meta_.push_back(CommitMeta{strings_.add(summary), strings_.add(author), when});
    return index;
}

std::string CommitStore::shortId(CommitIndex i, std::size_t chars) const {
    // Sized for SHA-256 (64 hex chars) rather than using GIT_OID_MAX_HEXSIZE,
    // which only exists in libgit2 1.8+. Ubuntu 24.04 LTS still ships 1.7.2,
    // and there is nothing else here that needs the newer library.
    constexpr std::size_t kMaxHexChars = 64;
    char buffer[kMaxHexChars + 1] = {};
    git_oid_tostr(buffer, sizeof(buffer), &oids_[i]);
    return std::string(buffer, std::min(chars, std::strlen(buffer)));
}

void CommitStore::clear() {
    oids_.clear();
    meta_.clear();
    strings_.clear();
}

} // namespace gity::model
