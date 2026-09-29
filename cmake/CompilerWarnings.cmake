# ADR-006 — warnings as errors on all three compilers, from the first commit.
#
# Opt out per-target only with a comment explaining why. GITY_WERROR is off by
# default for local builds and forced on in CI.

option(GITY_WERROR "Treat warnings as errors" OFF)

add_library(gity_warnings INTERFACE)
add_library(gity::warnings ALIAS gity_warnings)

if(MSVC)
    # C4996 fires on standard C++ (getenv, strncpy, ...) because MSVC prefers
    # its own _s variants, which do not exist anywhere else. Suppressing the
    # class is right for portable code; it does not weaken any real warning.
    target_compile_definitions(gity_warnings INTERFACE _CRT_SECURE_NO_WARNINGS)

    target_compile_options(gity_warnings INTERFACE
        /W4
        /permissive-
        /w14640  # thread-unsafe static member initialization
        /wd4275  # non dll-interface base, noisy with Qt
    )
    if(GITY_WERROR)
        target_compile_options(gity_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(gity_warnings INTERFACE
        -Wall
        -Wextra
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
    )
    if(GITY_WERROR)
        target_compile_options(gity_warnings INTERFACE -Werror)
    endif()
endif()
