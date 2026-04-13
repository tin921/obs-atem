cmake_minimum_required(VERSION 3.16)
project(obs-atem VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ── Find OBS Studio SDK ──────────────────────────────────────
# Set OBS_DIR to your obs-studio source or install path
# e.g. cmake -DOBS_DIR=C:/obs-studio ...
if(NOT OBS_DIR)
    set(OBS_DIR "C:/obs-studio" CACHE PATH "OBS Studio source/install directory")
endif()

# OBS libraries and headers
find_path(OBS_INCLUDE_DIR
    NAMES obs-module.h
    PATHS
        ${OBS_DIR}/libobs
        ${OBS_DIR}/include
        ${OBS_DIR}/UI/obs-frontend-api
        ${OBS_DIR}/frontend/api
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
if(NOT Qt6_FOUND)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets Core Gui)
    set(QT_LIBS Qt5::Widgets Qt5::Core Qt5::Gui)
else()
    set(QT_LIBS Qt6::Widgets Qt6::Core Qt6::Gui)
endif()

# ── Find Blackmagic ATEM SDK ─────────────────────────────────
# Set ATEM_SDK_DIR to your BMD Switcher SDK include path
# Usually: C:/Program Files (x86)/Blackmagic Design/Blackmagic ATEM Switchers/SDK
if(NOT ATEM_SDK_DIR)
    set(ATEM_SDK_DIR "C:/Program Files (x86)/Blackmagic Design/Blackmagic ATEM Switchers/ATEM SDK"
        CACHE PATH "Blackmagic ATEM SDK directory")
endif()

# ── Plugin Target ────────────────────────────────────────────
add_library(obs-atem MODULE
    plugin-main.cpp
    atem-controller.h
    atem-controller.cpp
    macro-dock.h
    macro-dock.cpp
    settings-dialog.h
    settings-dialog.cpp
    bmd-guids.cpp
)

target_include_directories(obs-atem PRIVATE
    ${OBS_INCLUDE_DIR}
    ${OBS_DIR}/UI/obs-frontend-api
    ${OBS_DIR}/frontend/api
    ${ATEM_SDK_DIR}/include
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(obs-atem PRIVATE
    ${OBS_LIB}
    ${OBS_FRONTEND_LIB}
    ${QT_LIBS}
    ole32
    oleaut32
    uuid
)

# ── Install ──────────────────────────────────────────────────
# Copy to OBS plugin directory
install(TARGETS obs-atem
    LIBRARY DESTINATION "obs-plugins/64bit"
    RUNTIME DESTINATION "obs-plugins/64bit"
)
