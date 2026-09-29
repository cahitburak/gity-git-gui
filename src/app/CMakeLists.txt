if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR
        "src/app/ is a component of the Gity project, not a project on its own.\n"
        "Open the repository root CMakeLists.txt instead — the one next to README.md.\n"
        "In Qt Creator: File -> Close All Projects and Editors, then "
        "File -> Open File or Project and pick the TOP-LEVEL CMakeLists.txt.")
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)

qt_standard_project_setup()

qt_add_executable(gity_app
    MainWindow.cpp
    MainWindow.h
    main.cpp
)

set_target_properties(gity_app PROPERTIES
    OUTPUT_NAME gity
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "Gity"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.cahitburak.gity_git_gui"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

# One version, from project() — shown in Help ▸ About and --version.
target_compile_definitions(gity_app PRIVATE GITY_VERSION="${PROJECT_VERSION}")

target_link_libraries(gity_app
    PRIVATE gity::ui
    PRIVATE gity::warnings
)

install(TARGETS gity_app
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(UNIX AND NOT APPLE)
    install(FILES ${CMAKE_SOURCE_DIR}/packaging/io.github.cahitburak.gity_git_gui.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES ${CMAKE_SOURCE_DIR}/packaging/io.github.cahitburak.gity_git_gui.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    )
    # AppStream: what software centres show for the app.
    install(FILES ${CMAKE_SOURCE_DIR}/packaging/io.github.cahitburak.gity_git_gui.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
    )
endif()
