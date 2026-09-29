if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR
        "bench/ is a component of the Gity project, not a project on its own.\n"
        "Open the repository root CMakeLists.txt instead — the one next to README.md.\n"
        "In Qt Creator: File -> Close All Projects and Editors, then "
        "File -> Open File or Project and pick the TOP-LEVEL CMakeLists.txt.")
endif()

add_executable(gity_bench main.cpp)
set_target_properties(gity_bench PROPERTIES OUTPUT_NAME gity-bench)

target_link_libraries(gity_bench
    PRIVATE gity::core
    PRIVATE gity::warnings
)
