if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR
        "src/askpass/ is a component of the Gity project, not a project on its own.\n"
        "Open the repository root CMakeLists.txt instead — the one next to README.md.\n"
        "In Qt Creator: File -> Close All Projects and Editors, then "
        "File -> Open File or Project and pick the TOP-LEVEL CMakeLists.txt.")
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)

qt_standard_project_setup()

# Not a bundle on macOS and not WIN32_EXECUTABLE on Windows: git runs this as a
# child process and reads its stdout, so it has to stay an ordinary executable
# that a parent can find on a path and pipe.
qt_add_executable(gity_askpass main.cpp)

set_target_properties(gity_askpass PROPERTIES OUTPUT_NAME gity-askpass)

target_link_libraries(gity_askpass
    PRIVATE gity::ui
    PRIVATE gity::warnings
)

# Beside gity, never elsewhere: GitProcess locates the helper relative to the
# running executable and never from PATH. Splitting them across directories
# silently turns credential prompting off.
install(TARGETS gity_askpass RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
