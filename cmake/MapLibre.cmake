# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# MapLibre Native Qt for the Qt layers: vcpkg builds it (manifest feature
# "gui", overlay port cmake/vcpkg-ports/maplibre-native-qt) against the Qt in
# $QT_ROOT_DIR. Included by the top-level CMakeLists.txt after Qt is found.
#
# zlib: on Linux and macOS QMapLibre links the system libz.so, while the vcpkg
# ports (netCDF/HDF5) link vcpkg's static libz.a. The two never meet in one
# binary today (the Qt layers do not link io). Once the app links io, both
# land in one process; that works only while the two zlib versions stay ABI
# compatible, so prefer making the ports use the same zlib then.

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

# MSVC: the port builds QMapLibre in release only, linked to release Qt. A
# Debug app links Qt's debug DLLs (Qt6Cored.dll ...) and the debug CRT, so one
# process would hold two QtCores and two CRTs. Reject it here instead.
if(MSVC)
    if(CMAKE_CONFIGURATION_TYPES)
        message(
            FATAL_ERROR
            "MOV_ENABLE_QT with MSVC needs a single-configuration generator (Ninja): MapLibre Native Qt is "
            "release-only, and a Debug configuration would mix debug and release Qt and CRTs."
        )
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(
            FATAL_ERROR
            "MOV_ENABLE_QT with MSVC needs a release-type CMAKE_BUILD_TYPE (Release, RelWithDebInfo): "
            "MapLibre Native Qt is release-only, and a Debug app would mix debug and release Qt and CRTs."
        )
    endif()
endif()

# The QMapLibre shared libraries an executable needs at run time. The
# packaging step (install, windeployqt, macdeployqt, linuxdeploy) ships these
# plus QMapLibre::PluginGeoServices.
set(MOV_MAPLIBRE_RUNTIME_LIBRARIES QMapLibre::Core QMapLibre::Location QMapLibre::QuickPrivate)
# The Qt libraries those binaries and the plugin link (readelf -d).
set(MOV_MAPLIBRE_QT_DEPENDENCIES
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Sql
    Qt6::Qml
    Qt6::Quick
    Qt6::Location
    Qt6::Positioning
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Link items that stay in DT_NEEDED even though the executable calls none
    # of their symbols (the toolchain default is --as-needed).
    set(CMAKE_LINK_LIBRARY_USING_MOV_NO_AS_NEEDED
        "LINKER:--push-state,--no-as-needed"
        "<LINK_ITEM>"
        "LINKER:--pop-state"
    )
    set(CMAKE_LINK_LIBRARY_USING_MOV_NO_AS_NEEDED_SUPPORTED TRUE)
endif()

# mov_stage_maplibre_runtime(<executable>)
#
# Makes MapLibre loadable when <executable> runs from the build tree, with no
# QT_PLUGIN_PATH, PATH or LD_LIBRARY_PATH. Installed layouts are the packaging
# step's job.
#  - Copies the "maplibre" geoservices plugin where Qt looks for plugins:
#    <dir of executable>/geoservices, or Contents/PlugIns/geoservices in a
#    macOS bundle.
#  - Windows: copies MOV_MAPLIBRE_RUNTIME_LIBRARIES next to the executable
#    (TARGET_RUNTIME_DLLS would miss QuickPrivate, which only the plugin and
#    QMapLibreLocation need).
#  - Linux: the invariant is that the executable keeps every library the
#    plugin chain needs resident. vcpkg gives the QMapLibre libraries RUNPATH
#    $ORIGIN, which finds each other but not Qt, and RUNPATH does not reach a
#    library's own dependencies; the copied plugin's RUNPATH ($ORIGIN/../../lib)
#    no longer points at the vcpkg tree at all. Rewriting the plugin's RUNPATH
#    would not help libQMapLibre find Qt6Sql, so instead the executable lists
#    QMapLibre::Location and the Qt dependencies in DT_NEEDED (scoped
#    --no-as-needed), resolved through its own RUNPATH. The loader then
#    satisfies the libraries' and the plugin's needs by soname.
function(mov_stage_maplibre_runtime target)
    get_target_property(is_bundle ${target} MACOSX_BUNDLE)
    if(APPLE AND is_bundle)
        set(plugin_dir "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/PlugIns/geoservices")
    else()
        set(plugin_dir "$<TARGET_FILE_DIR:${target}>/geoservices")
    endif()
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${plugin_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:QMapLibre::PluginGeoServices>" "${plugin_dir}"
        VERBATIM
    )
    if(WIN32)
        list(TRANSFORM MOV_MAPLIBRE_RUNTIME_LIBRARIES PREPEND "$<TARGET_FILE:" OUTPUT_VARIABLE dlls)
        list(TRANSFORM dlls APPEND ">")
        add_custom_command(
            TARGET ${target}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${dlls} "$<TARGET_FILE_DIR:${target}>"
            VERBATIM
        )
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # The libraries' files, not their targets: CMake rejects a target
        # linked both with a feature and (through mov::ui) without one.
        set(resident QMapLibre::Location ${MOV_MAPLIBRE_QT_DEPENDENCIES})
        list(TRANSFORM resident PREPEND "$<TARGET_LINKER_FILE:")
        list(TRANSFORM resident APPEND ">")
        list(JOIN resident "," resident)
        target_link_libraries(${target} PRIVATE "$<LINK_LIBRARY:MOV_NO_AS_NEEDED,${resident}>")
    endif()
endfunction()
