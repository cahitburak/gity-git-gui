# ADR-002 — Widgets shell, with the hot views custom-painted rather than
# composed from widgets.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)

add_library(gity_ui STATIC
    image/ImageDiffView.cpp
    image/ImageDiffView.h
    diffview/DiffView.cpp
    diffview/DiffView.h
    graphview/GraphColumns.h
    graphview/GraphHeader.cpp
    graphview/GraphHeader.h
    graphview/CommitGraphView.cpp
    graphview/CommitGraphView.h
    panels/WorkingCopyDelegate.cpp
    panels/WorkingCopyDelegate.h
    panels/WorkingCopyPanel.cpp
    panels/WorkingCopyPanel.h
    panels/CloneDialog.cpp
    panels/AskpassDialog.cpp
    panels/CredentialsPanel.cpp
    panels/WelcomePanel.cpp
    panels/CommandLogView.cpp
    panels/PreviewDialog.cpp
    panels/RemotesDialog.cpp
    panels/OperationBanner.cpp
    panels/RebaseDialog.cpp
    panels/RefsSidebarDelegate.cpp
    panels/RepositoryTabs.cpp
    panels/AccountsPanel.cpp
    panels/Confirm.cpp
    panels/SettingsDialog.cpp
    panels/CommitDetailPanel.cpp
    panels/CommitDetailPanel.h
    panels/RefsSidebar.cpp
    panels/RefsSidebar.h
    model/HistoryModel.cpp
    model/HistoryModel.h
    theme/Theme.cpp
    theme/ToolbarIcons.cpp
    theme/Theme.h
)
add_library(gity::ui ALIAS gity_ui)

set_target_properties(gity_ui PROPERTIES AUTOMOC ON AUTORCC ON)

# The stylesheet is compiled in rather than installed beside the binary: a
# theme file that can go missing turns a working application into an unstyled
# one, and there is nothing a user is meant to edit here — the colours come
# from TOKENS.json.
target_sources(gity_ui PRIVATE theme/theme.qrc)

target_link_libraries(gity_ui
    PUBLIC gity::session
    PUBLIC Qt6::Widgets
    PRIVATE gity::warnings
)
