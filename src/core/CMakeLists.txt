# ADR-006 — core links and runs headless. No Qt, no display, no widgets.
# If a test of this library ever needs QtWidgets, the layering has been
# violated and the fix is in the layering, not in the test.

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR
        "src/core/ is a component of the Gity project, not a project on its own.\n"
        "Open the repository root CMakeLists.txt instead — the one next to README.md.\n"
        "In Qt Creator: File -> Close All Projects and Editors, then "
        "File -> Open File or Project and pick the TOP-LEVEL CMakeLists.txt.")
endif()

add_library(gity_core STATIC
    git/GitError.h
    git/Stash.cpp
    git/Stash.h
    git/Staging.cpp
    git/NetworkOutcome.cpp
    git/RemoteUrl.cpp
    git/AskpassPrompt.cpp
    git/OperationState.cpp
    git/RebaseTodo.cpp
    git/Submodules.cpp
    git/HunkStaging.cpp
    git/LfsLocks.cpp
    git/Provider.cpp
    git/PullRequest.cpp
    git/DeviceFlow.cpp
    git/Remotes.cpp
    git/Staging.h
    git/WorkingCopy.cpp
    git/WorkingCopy.h
    git/FileDiff.cpp
    git/FileDiff.h
    git/BlobReader.cpp
    git/BlobReader.h
    git/CommitDetail.cpp
    git/Compare.cpp
    git/Credential.cpp
    git/CredentialSources.cpp
    git/RepoAccount.cpp
    git/UndoPlan.cpp
    git/MergePrediction.cpp
    git/CommitDetail.h
    git/Refs.cpp
    git/Refs.h
    git/HistoryStream.cpp
    git/HistoryStream.h
    git/Repository.cpp
    git/Repository.h
    graph/GraphRow.h
    graph/LaneAssigner.cpp
    graph/LaneAssigner.h
    model/CommitStore.cpp
    model/EnglishPlural.cpp
    model/EnglishPlural.h
    model/CommitStore.h
    model/HistoryChunk.h
)
add_library(gity::core ALIAS gity_core)

target_include_directories(gity_core PUBLIC "${CMAKE_SOURCE_DIR}/src")

target_link_libraries(gity_core
    PUBLIC gity::libgit2
    PRIVATE gity::warnings
)
