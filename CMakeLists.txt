# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# 4.4: the only CMake version CI and the dev image exercise (tools/versions.env).
cmake_minimum_required(VERSION 4.4)

# The vcpkg toolchain installs the manifest during the first project() call,
# so the features must be chosen before it. The Qt layers need the map
# (vcpkg.json feature "gui": MapLibre Native Qt).
if(MOV_ENABLE_QT)
    list(APPEND VCPKG_MANIFEST_FEATURES gui)
endif()

# The single source of truth for the version: cmake/version.hpp.in, Info.plist,
# the Windows VERSIONINFO and manifest, and the CPack metadata are generated
# from it (cmake/Packaging.cmake, packaging/).
project(
    MetOceanViewer
    VERSION 5.0.0
    DESCRIPTION "Viewer for hydrodynamic model output and observed met-ocean data"
    HOMEPAGE_URL "https://github.com/zcobell/MetOceanViewer"
    LANGUAGES CXX
)

include(cmake/PreventInSourceBuilds.cmake)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# C++20 modules are not used; skip the dependency scan.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(NOT DEFINED BUILD_TESTING)
    set(BUILD_TESTING ${PROJECT_IS_TOP_LEVEL} CACHE BOOL "Build the testing tree")
endif()
include(CTest)

include(cmake/ProjectOptions.cmake)
mov_declare_options()
mov_create_option_targets()

include(cmake/Layering.cmake)

# Qt and the map, for src/ui and tests/ui; the layering check keeps them out
# of core and io. The exact Qt is QT_VERSION in tools/versions.env; this is
# the oldest the build accepts.
set(MOV_QT_MINIMUM_VERSION 6.11)
if(MOV_ENABLE_QT)
    # Qt Gui's link interface carries OpenGL; on Linux link it as libGL.so.1,
    # which every system with OpenGL has, not GLVND's libOpenGL.so.0, which
    # many lack (the MapLibre port does the same; docs/packaging.md).
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(OpenGL_GL_PREFERENCE LEGACY)
    endif()
    find_package(
        Qt6
        ${MOV_QT_MINIMUM_VERSION}
        REQUIRED
        COMPONENTS Gui Network Sql Qml Quick QuickControls2 Location Positioning Test
    )
    qt_standard_project_setup(REQUIRES ${MOV_QT_MINIMUM_VERSION})
    include(cmake/MapLibre.cmake)
    include(cmake/Packaging.cmake)
    if(WIN32)
        # The application's VERSIONINFO and icon (packaging/windows/).
        enable_language(RC)
    endif()
endif()

add_subdirectory(src)

# The packages of the application (CPack), after src/ has declared its
# install rules.
if(MOV_ENABLE_QT)
    add_subdirectory(packaging)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
    if(MOV_ENABLE_COVERAGE)
        mov_add_coverage_target()
    endif()
endif()

# Runs last, once every target and its link interface exist.
mov_check_layering()
