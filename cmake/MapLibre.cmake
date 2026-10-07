# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# MapLibre Native Qt for the Qt layers: vcpkg builds it (manifest feature
# "gui", overlay port cmake/vcpkg-ports/maplibre-native-qt) against the Qt in
# $QT_ROOT_DIR. Included by the top-level CMakeLists.txt after Qt is found.

find_package(QMapLibre CONFIG REQUIRED COMPONENTS Location)

# QMapLibre's Location plugin uses Qt private API, so it must have been built
# against exactly this Qt. A Qt bump that misses the port would otherwise reuse
# a stale binary from the vcpkg cache.
include("${QMapLibre_DIR}/mov-qt-version.cmake")
if(NOT MOV_MAPLIBRE_QT_VERSION VERSION_EQUAL Qt6_VERSION)
    message(
        FATAL_ERROR
        "MapLibre Native Qt was built against Qt ${MOV_MAPLIBRE_QT_VERSION}, but this build uses Qt ${Qt6_VERSION}. "
        "Pin the new version in cmake/vcpkg-ports/maplibre-native-qt/portfile.cmake (mov_qt_version)."
    )
endif()

# mov_stage_maplibre_runtime(<executable>)
#
# Makes MapLibre loadable when <executable> runs from the build tree, with no
# QT_PLUGIN_PATH or LD_LIBRARY_PATH. Installed layouts are the packaging
# step's job.
#  - Copies the "maplibre" geoservices plugin into <dir of executable>/
#    geoservices, where Qt looks for plugins next to the executable.
#  - Linux: vcpkg gives the QMapLibre libraries RUNPATH $ORIGIN, and RUNPATH
#    does not reach a library's own dependencies, so the Qt libraries they need
#    (Qt6Sql, ...) are found only if the executable already loads them.
#    --no-as-needed keeps every Qt library on its link line in DT_NEEDED,
#    resolved through the executable's RUNPATH.
function(mov_stage_maplibre_runtime target)
    set(plugin_dir "$<TARGET_FILE_DIR:${target}>/geoservices")
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${plugin_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:QMapLibre::PluginGeoServices>" "${plugin_dir}"
        VERBATIM
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(${target} PRIVATE "LINKER:--no-as-needed")
    endif()
endfunction()
