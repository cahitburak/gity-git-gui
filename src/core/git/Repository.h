// ADR-003 / ADR-004 — RAII over libgit2.
//
// Handles are thread-confined by contract: one git_repository per worker
// thread, and nothing derived from libgit2 ever crosses a thread boundary.
// Nothing here enforces that yet; RepoSession will.
#pragma once

#include "GitError.h"

#include <git2.h>

#include <string>
#include <utility>

namespace gity::git {

/// Refcounted library init. Construct one before touching anything else.
class LibGit2 {
public:
    LibGit2();
    ~LibGit2();

    LibGit2(const LibGit2&) = delete;
    LibGit2& operator=(const LibGit2&) = delete;
};

/// Translates the thread-local libgit2 error into our vocabulary.
[[noreturn]] void throwLastError(int code, const char* what);

inline void check(int code, const char* what) {
    if (code < 0) {
        throwLastError(code, what);
    }
}

/// Owning handle for a libgit2 object with a known free function.
template <typename T, void (*FreeFn)(T*)>
class Handle {
public:
    Handle() = default;
    explicit Handle(T* raw) noexcept : raw_(raw) {}

    ~Handle() { reset(); }

    Handle(Handle&& other) noexcept : raw_(std::exchange(other.raw_, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset();
            raw_ = std::exchange(other.raw_, nullptr);
        }
        return *this;
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    void reset(T* raw = nullptr) noexcept {
        if (raw_ != nullptr) {
            FreeFn(raw_);
        }
        raw_ = raw;
    }

    [[nodiscard]] T* get() const noexcept { return raw_; }
    [[nodiscard]] T** receive() noexcept {
        reset();
        return &raw_;
    }
    explicit operator bool() const noexcept { return raw_ != nullptr; }

private:
    T* raw_ = nullptr;
};

using RepositoryHandle = Handle<git_repository, git_repository_free>;
using RevwalkHandle = Handle<git_revwalk, git_revwalk_free>;
using CommitHandle = Handle<git_commit, git_commit_free>;
using TreeHandle = Handle<git_tree, git_tree_free>;
using DiffHandle = Handle<git_diff, git_diff_free>;
using StatusListHandle = Handle<git_status_list, git_status_list_free>;
using IndexHandle = Handle<git_index, git_index_free>;
using ObjectHandle = Handle<git_object, git_object_free>;
using ReferenceHandle = Handle<git_reference, git_reference_free>;
using BranchIteratorHandle = Handle<git_branch_iterator, git_branch_iterator_free>;

/// Opens a repository, discovering it from any path inside the worktree.
[[nodiscard]] RepositoryHandle openRepository(const std::string& path);

} // namespace gity::git
