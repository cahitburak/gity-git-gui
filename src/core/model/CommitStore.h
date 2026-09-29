// ADR-005 — commit metadata in arenas, not in per-commit heap allocations.
//
// Summaries and author names are interned into one contiguous buffer and
// referenced by (offset, length). At a million commits this is the difference
// between tens of megabytes and hundreds.
#pragma once

#include <git2.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gity::model {

using CommitIndex = std::uint32_t;

class StringArena {
public:
    struct Ref {
        std::uint32_t offset = 0;
        std::uint32_t length = 0;
    };

    Ref add(std::string_view text);

    [[nodiscard]] std::string_view view(Ref ref) const noexcept {
        return {data_.data() + ref.offset, ref.length};
    }

    [[nodiscard]] std::size_t bytes() const noexcept { return data_.size(); }

    void reserve(std::size_t bytes) { data_.reserve(bytes); }
    void clear() { data_.clear(); }

private:
    std::vector<char> data_;
};

struct CommitMeta {
    StringArena::Ref summary;
    StringArena::Ref author;
    std::int64_t when = 0; ///< Author time, seconds since epoch.
};

/// Append-only store for one contiguous run of the walk.
class CommitStore {
public:
    void reserve(std::size_t commits);

    CommitIndex append(const git_oid& oid, std::string_view summary, std::string_view author,
                       std::int64_t when);

    [[nodiscard]] std::size_t size() const noexcept { return oids_.size(); }
    [[nodiscard]] bool empty() const noexcept { return oids_.empty(); }

    [[nodiscard]] const git_oid& oid(CommitIndex i) const noexcept { return oids_[i]; }
    [[nodiscard]] std::string_view summary(CommitIndex i) const noexcept {
        return strings_.view(meta_[i].summary);
    }
    [[nodiscard]] std::string_view author(CommitIndex i) const noexcept {
        return strings_.view(meta_[i].author);
    }
    [[nodiscard]] std::int64_t when(CommitIndex i) const noexcept { return meta_[i].when; }

    /// Short hex form, for display.
    [[nodiscard]] std::string shortId(CommitIndex i, std::size_t chars = 8) const;

    [[nodiscard]] std::size_t bytes() const noexcept {
        return oids_.size() * sizeof(git_oid) + meta_.size() * sizeof(CommitMeta) +
               strings_.bytes();
    }

    void clear();

private:
    std::vector<git_oid> oids_;
    std::vector<CommitMeta> meta_;
    StringArena strings_;
};

} // namespace gity::model
