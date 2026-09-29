# ADR-004 — the threading layer. QtCore only: this is where libgit2 results are
# turned into immutable value types, and the last place that knows libgit2
# exists. Deliberately separate from core/ so core stays Qt-free and the
# headless CI leg keeps proving it.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network)

add_library(gity_session STATIC
    ImageDiff.h
    Accounts.cpp
    BrowserSignIn.cpp
    CredentialManager.cpp
    CommandLog.cpp
    CredentialInventory.cpp
    UndoLog.cpp
    GitProcess.cpp
    GitProcess.h
    RepoSession.cpp
    RepoSession.h
)
add_library(gity::session ALIAS gity_session)

set_target_properties(gity_session PROPERTIES AUTOMOC ON)

# The desktop keyring is reached over D-Bus, which only Linux has; elsewhere
# the Credentials page lists what it can without it.
find_package(Qt6 6.8 QUIET COMPONENTS DBus)
if(TARGET Qt6::DBus)
    target_link_libraries(gity_session PRIVATE Qt6::DBus)
    target_compile_definitions(gity_session PRIVATE GITY_HAVE_DBUS)
endif()

target_link_libraries(gity_session
    PUBLIC gity::core
    PUBLIC Qt6::Core
    PUBLIC Qt6::Network
    PUBLIC Qt6::Gui
    PRIVATE gity::warnings
)
