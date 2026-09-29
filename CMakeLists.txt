cmake_minimum_required(VERSION 3.24)

project(Gity
    VERSION 0.1.0
    DESCRIPTION "A fast, cross-platform Git client"
    LANGUAGES CXX)

# ADR-001: C++20, not 23 — AppleClang trails the open-source compilers.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# ADR-001 / ADR-007 — this is a desktop application: Linux, macOS, Windows.
# Qt Creator registers a kit for every component in a Qt install, so an
# Android or WebAssembly kit is one checkbox away on the Configure Project
# screen. Neither has libgit2, GoogleTest, or a `git` binary to shell out to,
# and the resulting failure is a wall of unrelated errors. Fail here instead,
# with a sentence that says what to do.
if(ANDROID OR IOS OR EMSCRIPTEN
   OR CMAKE_SYSTEM_NAME STREQUAL "Android"
   OR CMAKE_SYSTEM_NAME STREQUAL "iOS"
   OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    message(FATAL_ERROR
        "gity targets desktop platforms only, but this build is configured "
        "for ${CMAKE_SYSTEM_NAME}.\n"
        "In Qt Creator: Projects (left sidebar) -> Build & Run -> select a "
        "Desktop kit, e.g. \"Desktop Qt 6.11.1 GCC 64bit\".")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(CompilerWarnings)
include(Libgit2)

option(GITY_BUILD_APP "Build the Qt application shell" ON)
option(GITY_BUILD_BENCH "Build the M0 benchmark" ON)
option(GITY_BUILD_TESTS "Build the test suite" ON)

# Surfaces the non-source files in IDE project trees (Qt Creator, CLion,
# Visual Studio). Builds nothing — without it these are editable only by
# leaving the IDE.
add_custom_target(project_files SOURCES
    README.md
    ARCHITECTURE.md
    ROADMAP.md
    CHANGELOG.md
    CONTRIBUTING.md
    SECURITY.md
    THIRD_PARTY_NOTICES.md
    CMakePresets.json
    vcpkg.json
    .clang-format
    .gitignore
    .github/workflows/ci.yml
    tools/make-bench-repo.sh
)

include(GNUInstallDirs)

# The licence and third-party notices travel with every install and package
# (THIRD_PARTY_NOTICES.md says why).
install(FILES LICENSE THIRD_PARTY_NOTICES.md README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

add_subdirectory(src/core)

if(GITY_BUILD_APP)
    add_subdirectory(src/session)
    add_subdirectory(src/ui)
    add_subdirectory(src/app)
    add_subdirectory(src/askpass)
endif()

if(GITY_BUILD_BENCH)
    add_subdirectory(bench)
endif()

if(GITY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "")
message(STATUS "gity ${PROJECT_VERSION}")
message(STATUS "  build type ... ${CMAKE_BUILD_TYPE}")
message(STATUS "  app ......... ${GITY_BUILD_APP}")
message(STATUS "  bench ....... ${GITY_BUILD_BENCH}")
message(STATUS "  tests ....... ${GITY_BUILD_TESTS}")
message(STATUS "")

# --- packaging -------------------------------------------------------------
#
# Archives only, and deliberately: they need no network and no downloads. A
# self-contained AppImage, a signed DMG and a Windows installer all need tools
# or identities that are somebody's decision to acquire — see packaging/README.md.
set(CPACK_PACKAGE_NAME "Gity")
set(CPACK_PACKAGE_VENDOR "Gity")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A fast, cross-platform Git client")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "gity-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_STRIP_FILES ON)
set(CPACK_SOURCE_GENERATOR "")
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)
