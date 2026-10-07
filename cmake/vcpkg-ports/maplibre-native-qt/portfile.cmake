# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# MapLibre Native Qt, built from a pinned commit of `main`: no release supports
# Qt 6.11 (v3.0.0 targets Qt <= 6.7; main's CI targets Qt 6.11.2).
#
# Qt is not a vcpkg dependency; it comes from $QT_ROOT_DIR (aqtinstall), like
# the rest of the project. The QMapLibre Location plugin uses Qt *private*
# API, so a build is valid only for the exact Qt it was compiled against.
# vcpkg's ABI hash cannot see an external Qt, so the Qt version is pinned
# here (it must equal QT_VERSION in tools/versions.env): bumping Qt means
# editing this file, which changes the hash and invalidates cached binaries.
# The port records the version in share/QMapLibre/mov-qt-version.cmake, and
# cmake/MapLibre.cmake checks it, so a stale cached binary fails the configure.

set(mov_qt_version 6.11.3)

# maplibre-native-qt `main` and the maplibre-native core it pins as a submodule.
set(mln_qt_ref c3485f3a9590081c7be8edbc542341e688f6663d) # 2026-09-20
# Core submodules the Qt build compiles, reads licenses from or needs at
# configure time (googletest and benchmark: core always defines its test
# targets, though they are not built). Vulkan/WebGPU, HarfBuzz/FreeType, the
# Windows vcpkg and the rest are left out. On a bump, compare with
# vendor/maplibre-native/.gitmodules.
set(mln_core_submodules
    vendor/args
    vendor/benchmark
    vendor/boost
    vendor/earcut.hpp
    vendor/eternal
    vendor/expected-lite
    vendor/filesystem
    vendor/googletest
    vendor/kdbush.hpp
    vendor/maplibre-native-base/deps/cheap-ruler-cpp
    vendor/maplibre-native-base/deps/geojson-vt-cpp
    vendor/maplibre-native-base/deps/geojson.hpp
    vendor/maplibre-native-base/deps/geometry.hpp
    vendor/maplibre-native-base/deps/jni.hpp
    vendor/maplibre-native-base/deps/pixelmatch-cpp
    vendor/maplibre-native-base/deps/shelf-pack-cpp
    vendor/maplibre-native-base/deps/variant
    vendor/maplibre-tile-spec
    vendor/metal-cpp
    vendor/PMTiles
    vendor/polylabel
    vendor/protozero
    vendor/rapidjson
    vendor/supercluster
    vendor/unique_resource
    vendor/unordered_dense
    vendor/vector-tile
    vendor/wagyu
)
# ...and those of vendor/maplibre-native/vendor/maplibre-tile-spec.
set(mln_tile_spec_submodules cpp/vendor/earcut cpp/vendor/fsst)

if(NOT DEFINED ENV{QT_ROOT_DIR})
    message(FATAL_ERROR "maplibre-native-qt needs Qt ${mov_qt_version}: set QT_ROOT_DIR to its prefix")
endif()
file(TO_CMAKE_PATH "$ENV{QT_ROOT_DIR}" qt_root)
file(STRINGS "${qt_root}/lib/cmake/Qt6/Qt6ConfigVersionImpl.cmake" qt_version_line REGEX "^set\\(PACKAGE_VERSION \"")
string(REGEX REPLACE "^set\\(PACKAGE_VERSION \"([^\"]*)\"\\)$" "\\1" qt_version "${qt_version_line}")
if(NOT qt_version STREQUAL mov_qt_version)
    message(
        FATAL_ERROR
        "QT_ROOT_DIR (${qt_root}) holds Qt '${qt_version}', but this port is pinned to Qt ${mov_qt_version}. "
        "Update mov_qt_version in ${CMAKE_CURRENT_LIST_FILE} together with QT_VERSION in tools/versions.env."
    )
endif()

# Qt builds shared libraries and plugins; QMapLibre follows it.
vcpkg_check_linkage(ONLY_DYNAMIC_LIBRARY)
# The app links release QMapLibre in every configuration; a debug copy would
# double a long build for nothing.
set(VCPKG_BUILD_TYPE release)

# The GitHub archives omit submodules, so fetch with git: shallow, at pinned
# commits (the core commit is pinned by mln_qt_ref's tree).
vcpkg_find_acquire_program(GIT)
string(SUBSTRING "${mln_qt_ref}" 0 10 short_ref)
set(SOURCE_PATH "${CURRENT_BUILDTREES_DIR}/src/${short_ref}")
function(mov_git step)
    vcpkg_execute_required_process(
        COMMAND "${GIT}" -c core.longpaths=true -c advice.detachedHead=false ${ARGN}
        WORKING_DIRECTORY "${SOURCE_PATH}"
        LOGNAME "git-${step}-${TARGET_TRIPLET}"
    )
endfunction()
# Unformatted: one git command per line reads better than gersemi's one token
# per line.
# gersemi: off
file(REMOVE_RECURSE "${SOURCE_PATH}")
file(MAKE_DIRECTORY "${SOURCE_PATH}")
mov_git(init init --quiet)
mov_git(fetch fetch --quiet --depth 1 https://github.com/maplibre/maplibre-native-qt.git ${mln_qt_ref})
mov_git(checkout checkout --quiet FETCH_HEAD)
# --single-branch: without it a shallow submodule clone fetches every branch
# and tag at depth 1.
set(shallow --init --depth 1 --single-branch --jobs 8 --)
mov_git(core submodule update ${shallow} vendor/maplibre-native)
mov_git(core-deps -C vendor/maplibre-native submodule update ${shallow} ${mln_core_submodules})
mov_git(tile-spec-deps -C vendor/maplibre-native/vendor/maplibre-tile-spec
        submodule update ${shallow} ${mln_tile_spec_submodules})
# gersemi: on

# The bindings' own tests are not installed; skip building them.
vcpkg_replace_string("${SOURCE_PATH}/CMakeLists.txt" "add_subdirectory(test)" "" IGNORE_UNCHANGED)

if(VCPKG_TARGET_IS_OSX)
    # Qt Quick renders through Metal on macOS; OpenGL is deprecated there.
    set(renderer -DMLN_WITH_METAL=ON)
else()
    set(renderer -DMLN_WITH_OPENGL=ON)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${renderer}
        "-DCMAKE_PREFIX_PATH=${qt_root}"
        -DQT_VERSION_MAJOR=6
        -DMLN_QT_WITH_LOCATION=ON
        -DMLN_QT_WITH_QUICK_PLUGIN=OFF
        -DMLN_QT_WITH_WIDGETS=OFF
        # Bundled ICU (Linux only): no system ICU to match at run time, as in
        # upstream's Linux release builds.
        -DMLN_QT_WITH_INTERNAL_ICU=ON
        # Third-party warnings must not fail our build on a newer compiler.
        -DMLN_WITH_WERROR=OFF
    MAYBE_UNUSED_VARIABLES
        MLN_QT_WITH_INTERNAL_ICU
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME QMapLibre CONFIG_PATH lib/cmake/QMapLibre)

file(
    WRITE "${CURRENT_PACKAGES_DIR}/share/QMapLibre/mov-qt-version.cmake"
    "# Written by the maplibre-native-qt overlay port; read by cmake/MapLibre.cmake.\n"
    "set(MOV_MAPLIBRE_QT_VERSION ${mov_qt_version})\n"
)
file(GLOB licenses "${SOURCE_PATH}/LICENSES/*.txt")
vcpkg_install_copyright(FILE_LIST ${licenses})
