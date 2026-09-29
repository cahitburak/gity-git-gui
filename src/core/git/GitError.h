// ADR-003 — one error vocabulary for both backends.
//
// libgit2 and the git CLI fail in different languages. Everything is
// normalized here so the UI layer never learns either dialect. For M0 this is
// an exception; the session layer (ADR-004) will convert it to a value type at
// the thread boundary, because exceptions do not cross queued signals.
#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace gity::git {

enum class ErrorSource {
    LibGit2,
    Cli,
    Internal,
};

struct GitError {
    ErrorSource source = ErrorSource::Internal;
    int code = 0;
    std::string message; ///< Shown to the user.
    std::string raw;     ///< Verbatim backend output, for a details disclosure.
};

class GitException : public std::runtime_error {
public:
    explicit GitException(GitError error)
        : std::runtime_error(error.message), error_(std::move(error)) {}

    [[nodiscard]] const GitError& error() const noexcept { return error_; }

private:
    GitError error_;
};

} // namespace gity::git
