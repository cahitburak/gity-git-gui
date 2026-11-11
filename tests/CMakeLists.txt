if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR
        "tests/ is a component of the Gity project, not a project on its own.\n"
        "Open the repository root CMakeLists.txt instead — the one next to README.md.\n"
        "In Qt Creator: File -> Close All Projects and Editors, then "
        "File -> Open File or Project and pick the TOP-LEVEL CMakeLists.txt.")
endif()

find_package(GTest REQUIRED)
include(GoogleTest)

# ADR-006 — this binary links core only, and builds without Qt (the
# headless leg). The session tests below add QtCore and nothing more. If a
# test ever needs QtWidgets, the layering has been violated.
add_executable(gity_tests
    test_blob_reader.cpp
    test_staging.cpp
    test_network_outcome.cpp
    test_remote_url.cpp
    test_askpass_prompt.cpp
    test_remotes.cpp
    test_operation_state.cpp
    test_rebase_todo.cpp
    test_working_diff.cpp
    test_hunk_staging.cpp
    test_lfs_locks.cpp
    test_provider.cpp
    test_device_flow.cpp
    test_lane_assigner.cpp
    test_version_sort.cpp
    test_credential.cpp
    test_credential_sources.cpp
    test_repo_account.cpp
    test_undo_plan.cpp
    test_merge_prediction.cpp
    test_refs_default.cpp
    test_pull_request.cpp
    test_english_plural.cpp
    test_history_stream.cpp
    test_commit_store.cpp
)

target_link_libraries(gity_tests
    PRIVATE gity::core
    PRIVATE gity::warnings
    PRIVATE GTest::gtest_main
)

gtest_discover_tests(gity_tests)

# The session layer runs real git, so its tests need QtCore — and only QtCore
# and the session library: QtWidgets here would still mean the layering broke.
# Not built by the headless leg, which has no Qt.
if(TARGET gity::session)
    add_executable(gity_session_tests
        session/main.cpp
        session/TempRepo.h
        session/test_index_writes.cpp
        session/test_git_process.cpp
        session/test_undo_log.cpp
        session/test_repo_watcher.cpp
        session/test_repository_reads.cpp
        session/test_session_hub.cpp
    )
    target_include_directories(gity_session_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(gity_session_tests
        PRIVATE gity::session
        PRIVATE gity::warnings
        PRIVATE GTest::gtest
    )
    gtest_discover_tests(gity_session_tests DISCOVERY_TIMEOUT 30)
endif()
