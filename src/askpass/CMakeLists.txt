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
if(APPLE AND TARGET gity_app)
    # On macOS gity is a bundle, and "beside it" means inside it, in
    # Contents/MacOS. Copied there at build time so a build-tree run prompts
    # too, and installed there so macdeployqt and signing see it as part of
    # the app.
    add_dependencies(gity_askpass gity_app)
    add_custom_command(TARGET gity_askpass POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:gity_askpass>
                $<TARGET_BUNDLE_CONTENT_DIR:gity_app>/MacOS/
        COMMENT "Placing gity-askpass inside gity.app"
        VERBATIM)
    install(TARGETS gity_askpass RUNTIME DESTINATION gity.app/Contents/MacOS)
else()
    install(TARGETS gity_askpass RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
