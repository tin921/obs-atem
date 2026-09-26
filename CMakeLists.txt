cmake_minimum_required(VERSION 3.16)
project(obs-atem VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Sources contain UTF-8 (UI glyphs like ⚙ ▶ ●). Without /utf-8 MSVC reads
# them in the system code page and the glyphs turn into mojibake.
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
# windows.h (pulled in by the BMD SDK header) defines min/max macros that
# break std::min/std::max depending on include order.
add_compile_definitions(NOMINMAX)

# ── Find OBS Studio SDK ──────────────────────────────────────
# Set OBS_DIR to your obs-studio source or install path
# e.g. cmake -DOBS_DIR=C:/obs-studio ...
# The OBS source version must not be newer than the OBS you run: OBS refuses
# to load plugins built against a newer libobs ("compiled with newer libobs").
if(NOT OBS_DIR)
    set(OBS_DIR "C:/obs-studio" CACHE PATH "OBS Studio source/install directory")
endif()

find_path(OBS_INCLUDE_DIR
    NAMES obs-module.h
    PATHS
        ${OBS_DIR}/libobs
        ${OBS_DIR}/include
)

find_path(OBS_FRONTEND_INCLUDE_DIR
    NAMES obs-frontend-api.h
    PATHS
        ${OBS_DIR}/frontend/api
        ${OBS_DIR}/UI/obs-frontend-api
        ${OBS_DIR}/include
)

find_library(OBS_LIB
    NAMES obs libobs
    PATHS
        ${OBS_DIR}/build/libobs/Release
        ${OBS_DIR}/lib
        ${OBS_DIR}/bin/64bit
        ${CMAKE_BINARY_DIR}
)

find_library(OBS_FRONTEND_LIB
    NAMES obs-frontend-api
    PATHS
        ${OBS_DIR}/build/UI/obs-frontend-api/Release
        ${OBS_DIR}/lib
        ${OBS_DIR}/bin/64bit
        ${CMAKE_BINARY_DIR}
)

# ── Find Qt (OBS 28+ uses Qt6, older uses Qt5) ──────────────
find_package(Qt6 COMPONENTS Widgets Core Gui QUIET)
if(Qt6_FOUND)
    set(QT_LIBS Qt6::Widgets Qt6::Core Qt6::Gui)
else()
    find_package(Qt5 5.15 COMPONENTS Widgets Core Gui QUIET)
    if(Qt5_FOUND)
        set(QT_LIBS Qt5::Widgets Qt5::Core Qt5::Gui)
    else()
        message(WARNING "Qt not found: only atem-cli will be built. Set Qt6_DIR.")
    endif()
endif()

# ── Find Blackmagic ATEM SDK ─────────────────────────────────
# Set ATEM_SDK_DIR to the SDK's Windows folder (contains include/BMDSwitcherAPI.h)
if(NOT ATEM_SDK_DIR)
    set(ATEM_SDK_DIR "C:/Program Files (x86)/Blackmagic Design/Blackmagic ATEM Switchers/ATEM SDK"
        CACHE PATH "Blackmagic ATEM SDK directory")
endif()

# ── Source sets ──────────────────────────────────────────────
# SDK layer: Qt-free, shared by every target.
set(ATEM_SDK_SOURCES
    bmd-util.h
    atem-controller.h
    atem-controller.cpp
    atem-pip.h
    atem-pip.cpp
    obs-log.h
)

# Panels: Qt, no OBS. Compiled unchanged into the plugin and the harness.
set(ATEM_PANEL_SOURCES
    ${ATEM_SDK_SOURCES}
    atem-session.h
    atem-session.cpp
    panel-common.h
    panel-common.cpp
    macro-dock.h
    macro-dock.cpp
    pip-dock.h
    pip-dock.cpp
    pip-preview.h
    pip-preview.cpp
    pip-settings.h
    pip-settings.cpp
    pip-widgets.h
    pip-widgets.cpp
    settings-dialog.h
    settings-dialog.cpp
)

set(COM_LIBS ole32 oleaut32 uuid)

# ── CLI diagnostics (no OBS, no Qt) ─────────────────────────
add_executable(atem-cli
    cli/main.cpp
    ${ATEM_SDK_SOURCES}
)
target_include_directories(atem-cli PRIVATE
    ${ATEM_SDK_DIR}/include
    ${CMAKE_SOURCE_DIR}
)
target_link_libraries(atem-cli PRIVATE ${COM_LIBS})

if(NOT QT_LIBS)
    return()
endif()

# ── Plugin Target ────────────────────────────────────────────
if(OBS_INCLUDE_DIR AND OBS_FRONTEND_INCLUDE_DIR AND OBS_LIB AND OBS_FRONTEND_LIB)
    add_library(obs-atem MODULE
        plugin-main.cpp
        ${ATEM_PANEL_SOURCES}
    )
    set_target_properties(obs-atem PROPERTIES AUTOMOC ON)

    target_include_directories(obs-atem PRIVATE
        ${OBS_INCLUDE_DIR}
        ${OBS_FRONTEND_INCLUDE_DIR}
        ${ATEM_SDK_DIR}/include
        ${CMAKE_SOURCE_DIR}
    )

    target_compile_definitions(obs-atem PRIVATE OBS_BUILD=1)

    target_link_libraries(obs-atem PRIVATE
        ${OBS_LIB}
        ${OBS_FRONTEND_LIB}
        ${QT_LIBS}
        ${COM_LIBS}
    )

    # Layout of an OBS plugin folder (for scripts\deploy.ps1 see README):
    #   <prefix>/bin/64bit/obs-atem.dll  (no data files)
    install(TARGETS obs-atem
        LIBRARY DESTINATION "bin/64bit"
        RUNTIME DESTINATION "bin/64bit"
    )
else()
    message(WARNING "OBS headers/import libraries not found: skipping the obs-atem plugin target. "
                    "Set OBS_DIR and generate obs.lib / obs-frontend-api.lib (see CLAUDE.md).")
endif()

# ── Standalone harness (Qt only, no OBS) ────────────────────
# Hosts the SAME panel sources in a bare QMainWindow so they can be run and
# debugged without OBS Studio. OBS_BUILD is deliberately NOT defined here, so
# obs-log.h routes blog() to stderr instead of libobs. Console subsystem so
# that output is visible when run from a terminal.
add_executable(atem-harness
    harness/main.cpp
    ${ATEM_PANEL_SOURCES}
)
set_target_properties(atem-harness PROPERTIES AUTOMOC ON)

target_include_directories(atem-harness PRIVATE
    ${ATEM_SDK_DIR}/include
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(atem-harness PRIVATE
    ${QT_LIBS}
    ${COM_LIBS}
)
