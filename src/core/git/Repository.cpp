#include "Repository.h"

namespace gity::git {

LibGit2::LibGit2() {
    check(git_libgit2_init(), "initialize libgit2");
}

LibGit2::~LibGit2() {
    git_libgit2_shutdown();
}

void throwLastError(int code, const char* what) {
    const git_error* err = git_error_last();
    GitError error;
    error.source = ErrorSource::LibGit2;
    error.code = code;
    error.raw = (err != nullptr && err->message != nullptr) ? err->message : "";
    error.message = std::string("Could not ") + what;
    if (!error.raw.empty()) {
        error.message += ": " + error.raw;
    }
    throw GitException(std::move(error));
}

RepositoryHandle openRepository(const std::string& path) {
    RepositoryHandle repo;
    check(git_repository_open_ext(repo.receive(), path.c_str(),
                                  GIT_REPOSITORY_OPEN_CROSS_FS, nullptr),
          "open repository");
    return repo;
}

} // namespace gity::git
