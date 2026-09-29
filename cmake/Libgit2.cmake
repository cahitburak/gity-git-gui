# ADR-006 — libgit2 comes from vcpkg on Windows and macOS, system packages on
# Linux. Both routes end at the same target name so nothing downstream cares.
#
# The lookup has to cope with three different ways libgit2 presents itself:
# upstream's own CMake config, vcpkg's "unofficial-" wrapper, and pkg-config on
# distributions. pkg-config is tried last and only where it exists — reaching
# for it on Windows produces "Could NOT find PkgConfig", which says nothing
# useful about the actual problem.

if(NOT TARGET gity::libgit2)
    set(_gity_libgit2_source "")

    # 1. CMake config packages. The package name is stable but the *target*
    #    name is not: upstream exports libgit2::libgit2, while vcpkg's port
    #    exports libgit2::libgit2package (its own usage message says so). Probe
    #    rather than assume — guessing here cost a full CI cycle.
    find_package(libgit2 CONFIG QUIET)
    find_package(unofficial-libgit2 CONFIG QUIET)

    foreach(_candidate
            libgit2::libgit2         # upstream >= 1.8
            libgit2::libgit2package  # vcpkg
            unofficial::libgit2::libgit2
            libgit2)
        if(NOT _gity_libgit2_source AND TARGET ${_candidate})
            add_library(gity::libgit2 ALIAS ${_candidate})
            set(_gity_libgit2_source "CONFIG (${_candidate})")
        endif()
    endforeach()

    # 3. pkg-config, for distributions. 1.7 is the floor because that is what
    #    Ubuntu 24.04 LTS ships; nothing here needs a newer libgit2.
    if(NOT _gity_libgit2_source AND NOT WIN32)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(LIBGIT2 IMPORTED_TARGET GLOBAL "libgit2>=1.7")
            if(TARGET PkgConfig::LIBGIT2)
                add_library(gity::libgit2 ALIAS PkgConfig::LIBGIT2)
                set(_gity_libgit2_source "pkg-config ${LIBGIT2_VERSION}")
            endif()
        endif()
    endif()

    if(NOT _gity_libgit2_source)
        message(FATAL_ERROR
            "Could not find libgit2 (>= 1.7).\n"
            "Tried: find_package(libgit2 CONFIG), find_package(unofficial-libgit2 CONFIG), "
            "and pkg-config where available.\n"
            "  Linux:   apt install libgit2-dev  (or your distribution's equivalent)\n"
            "  macOS:   brew install libgit2\n"
            "  Windows: configure with the vcpkg toolchain file, e.g.\n"
            "           -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake")
    endif()

    message(STATUS "libgit2 from ${_gity_libgit2_source}")
endif()
