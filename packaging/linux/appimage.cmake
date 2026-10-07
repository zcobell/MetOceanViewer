# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# CPACK_EXTERNAL_PACKAGE_SCRIPT of the Linux package: turns the staged install
# tree into an AppImage with linuxdeploy and its Qt plugin (docs/packaging.md).
#
# CPack has installed the project into CPACK_TEMPORARY_DIRECTORY with the
# prefix /usr, so that directory is the AppDir. This script then
#   1. downloads linuxdeploy, its Qt plugin and the AppImage runtime (pinned
#      in tools/versions.env, SHA-256 verified) into CPACK_MOV_TOOLS_DIR, once;
#   2. gives the Qt plugin a view of Qt without the plugins whose libraries
#      are not installed (SQL drivers other than SQLite, NMEA positioning);
#   3. bundles the build compiler's libstdc++ with the AppRun hook that
#      chooses between it and the system's at run time;
#   4. runs linuxdeploy, which bundles the Qt and system libraries, writes
#      AppRun and makes the AppImage;
#   5. checks the glibc floor (plan §6.24) and the libstdc++ hook against
#      every binary in the AppDir.
# Variables from packaging/CMakeLists.txt are named CPACK_MOV_*.

cmake_minimum_required(VERSION 4.4)

set(appdir "${CPACK_TEMPORARY_DIRECTORY}")
set(executable "${appdir}/usr/bin/metoceanviewer")
if(NOT EXISTS "${executable}")
    message(FATAL_ERROR "No ${executable}: the install tree is not an AppDir (CPACK_PACKAGING_INSTALL_PREFIX /usr?)")
endif()

# --- 1. linuxdeploy ----------------------------------------------------------

# Downloads <url> to <path> unless a file with the expected SHA-256 is there.
function(mov_fetch_verified url path sha256)
    if(EXISTS "${path}")
        file(SHA256 "${path}" actual)
        if(actual STREQUAL sha256)
            return()
        endif()
        file(REMOVE "${path}")
    endif()
    message(STATUS "Downloading ${url}")
    file(DOWNLOAD "${url}" "${path}.part" EXPECTED_HASH "SHA256=${sha256}" STATUS status TLS_VERIFY ON)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        file(REMOVE "${path}.part")
        message(FATAL_ERROR "Download of ${url} failed: ${status}")
    endif()
    file(RENAME "${path}.part" "${path}")
    file(
        CHMOD "${path}"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
    )
endfunction()

set(releases "https://github.com/linuxdeploy")
# linuxdeploy finds plugins named linuxdeploy-plugin-<name>-<arch>.AppImage
# beside itself.
set(linuxdeploy "${CPACK_MOV_TOOLS_DIR}/linuxdeploy-x86_64.AppImage")
mov_fetch_verified(
    "${releases}/linuxdeploy/releases/download/${CPACK_MOV_LINUXDEPLOY_VERSION}/linuxdeploy-x86_64.AppImage"
    "${linuxdeploy}"
    "${CPACK_MOV_LINUXDEPLOY_SHA256}"
)
mov_fetch_verified(
    "${releases}/linuxdeploy-plugin-qt/releases/download/${CPACK_MOV_LINUXDEPLOY_PLUGIN_QT_VERSION}/linuxdeploy-plugin-qt-x86_64.AppImage"
    "${CPACK_MOV_TOOLS_DIR}/linuxdeploy-plugin-qt-x86_64.AppImage"
    "${CPACK_MOV_LINUXDEPLOY_PLUGIN_QT_SHA256}"
)
# The runtime appimagetool prepends; without it, it downloads the moving
# "continuous" build.
set(runtime "${CPACK_MOV_TOOLS_DIR}/runtime-x86_64-${CPACK_MOV_LINUXDEPLOY_APPIMAGE_RUNTIME_VERSION}")
mov_fetch_verified(
    "https://github.com/AppImage/type2-runtime/releases/download/${CPACK_MOV_LINUXDEPLOY_APPIMAGE_RUNTIME_VERSION}/runtime-x86_64"
    "${runtime}"
    "${CPACK_MOV_LINUXDEPLOY_APPIMAGE_RUNTIME_SHA256}"
)

# --- 2. The Qt the plugin sees -------------------------------------------------

# The plugin deploys every plugin of a type it decides the application needs,
# and fails on one whose libraries are absent: the SQL drivers for client
# libraries that are not installed (libpq, ODBC, Mimer, ...; MapLibre's tile
# cache needs only SQLite), and the NMEA position plugin (Qt Serial Port, not
# installed). It asks qmake for Qt's paths, so a qmake wrapper with its own
# qt.conf points it at a plugin tree of symbolic links to Qt's plugins, minus
# those.
set(excluded_plugins "^sqldrivers/libqsql(ibase|mimer|mysql|oci|odbc|psql)\\.so$" "^position/libqtposition_nmea\\.so$")
set(qt_view "${CPACK_TOPLEVEL_DIRECTORY}/qt-view")
file(REMOVE_RECURSE "${qt_view}")
file(GLOB_RECURSE plugins LIST_DIRECTORIES false RELATIVE "${CPACK_MOV_QT_PLUGINS_DIR}" "${CPACK_MOV_QT_PLUGINS_DIR}/*")
foreach(plugin IN LISTS plugins)
    set(keep TRUE)
    foreach(pattern IN LISTS excluded_plugins)
        if(plugin MATCHES "${pattern}")
            set(keep FALSE)
        endif()
    endforeach()
    if(keep)
        get_filename_component(dir "${qt_view}/plugins/${plugin}" DIRECTORY)
        file(MAKE_DIRECTORY "${dir}")
        file(CREATE_LINK "${CPACK_MOV_QT_PLUGINS_DIR}/${plugin}" "${qt_view}/plugins/${plugin}" SYMBOLIC)
    endif()
endforeach()
file(WRITE "${qt_view}/qt.conf" "[Paths]\nPrefix=${CPACK_MOV_QT_PREFIX}\nPlugins=${qt_view}/plugins\n")
file(WRITE "${qt_view}/qmake" "#!/bin/sh\nexec '${CPACK_MOV_QMAKE}' -qtconf '${qt_view}/qt.conf' \"$@\"\n")
file(CHMOD "${qt_view}/qmake" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)

# --- 3. libstdc++ -------------------------------------------------------------

# The ELF files (not symbolic links) under <dir>.
function(mov_elf_files dir out_var)
    file(GLOB_RECURSE files LIST_DIRECTORIES false "${dir}/*")
    set(elf "")
    foreach(file IN LISTS files)
        if(NOT IS_SYMLINK "${file}")
            file(READ "${file}" magic LIMIT 4 HEX)
            if(magic STREQUAL "7f454c46")
                list(APPEND elf "${file}")
            endif()
        endif()
    endforeach()
    set(${out_var} "${elf}" PARENT_SCOPE)
endfunction()

# The highest GLIBCXX_/CXXABI_ symbol versions the ELF files under <dir>
# reference (the version names in their dynamic string tables; the bundled
# libstdc++ itself is skipped, it defines them all).
function(mov_required_stdcxx dir out_var)
    mov_elf_files("${dir}" files)
    list(FILTER files EXCLUDE REGEX "/usr/optional/")
    set(required "")
    foreach(family GLIBCXX CXXABI)
        set(best "")
        foreach(file IN LISTS files)
            file(STRINGS "${file}" names REGEX "^${family}_[0-9]+(\\.[0-9]+)+$")
            foreach(name IN LISTS names)
                string(REPLACE "${family}_" "" version "${name}")
                if(best STREQUAL "" OR version VERSION_GREATER best)
                    set(best "${version}")
                endif()
            endforeach()
        endforeach()
        if(NOT best STREQUAL "")
            list(APPEND required "${family}_${best}")
        endif()
    endforeach()
    set(${out_var} "${required}" PARENT_SCOPE)
endfunction()

# Built on the target glibc, the application's own binaries are what need the
# new libstdc++; Qt's and the system's need older versions (checked in 5.).
mov_required_stdcxx("${appdir}/usr" stdcxx_required)
list(JOIN stdcxx_required " " MOV_STDCXX_REQUIRED)
message(STATUS "The application needs ${MOV_STDCXX_REQUIRED} from libstdc++")
file(MAKE_DIRECTORY "${appdir}/usr/optional/libstdc++" "${appdir}/apprun-hooks")
foreach(library IN LISTS CPACK_MOV_BUNDLED_RUNTIME)
    # Under its soname (libstdc++.so.6.0.35 -> libstdc++.so.6), which the
    # loader looks for.
    get_filename_component(name "${library}" NAME)
    string(REGEX REPLACE "(\\.so\\.[0-9]+).*$" "\\1" soname "${name}")
    file(COPY_FILE "${library}" "${appdir}/usr/optional/libstdc++/${soname}")
endforeach()
configure_file(
    "${CPACK_MOV_PACKAGING_DIR}/linux/apprun-hooks/libstdcxx.sh.in"
    "${appdir}/apprun-hooks/libstdcxx.sh"
    @ONLY
)

# --- 4. linuxdeploy -----------------------------------------------------------

set(output "${CPACK_TOPLEVEL_DIRECTORY}/${CPACK_PACKAGE_FILE_NAME}.AppImage")
set(ENV{APPIMAGE_EXTRACT_AND_RUN} 1) # no FUSE in containers and CI
set(ENV{ARCH} x86_64)
set(ENV{LDAI_OUTPUT} "${output}")
set(ENV{OUTPUT} "${output}") # older appimage plugins
set(ENV{LDAI_RUNTIME_FILE} "${runtime}")
set(ENV{QMAKE} "${qt_view}/qmake")
# Where linuxdeploy resolves the Qt libraries (the executable's RUNPATH only
# covers usr/lib).
set(ENV{LD_LIBRARY_PATH} "${CPACK_MOV_QT_PREFIX}/lib")
# The QML the application compiles in, for qmlimportscanner.
set(ENV{QML_SOURCES_PATHS} "${CPACK_MOV_QML_SOURCES}")
# QIcon reads the SVG window icon through Qt Svg's plugins, which nothing
# links; offscreen runs the self-test headless.
set(ENV{EXTRA_QT_MODULES} "svg")
set(ENV{EXTRA_PLATFORM_PLUGINS} "libqoffscreen.so")
execute_process(
    COMMAND
        "${linuxdeploy}" --appdir "${appdir}" --executable "${executable}" --desktop-file
        "${appdir}/usr/share/applications/${CPACK_MOV_APP_ID}.desktop" --icon-file
        "${appdir}/usr/share/icons/hicolor/256x256/apps/${CPACK_MOV_APP_ID}.png" --deploy-deps-only
        "${appdir}/usr/plugins/geoservices" --plugin qt --output appimage
    WORKING_DIRECTORY "${CPACK_TOPLEVEL_DIRECTORY}"
    COMMAND_ERROR_IS_FATAL ANY
)
if(NOT EXISTS "${output}")
    message(FATAL_ERROR "linuxdeploy did not write ${output}")
endif()

# --- 5. Checks on the AppDir that was packed -----------------------------------

# glibc: nothing may need a newer one than the oldest supported system's.
mov_elf_files("${appdir}/usr" binaries)
set(glibc_needed "0")
foreach(file IN LISTS binaries)
    file(STRINGS "${file}" names REGEX "^GLIBC_[0-9]+(\\.[0-9]+)+$")
    foreach(name IN LISTS names)
        string(REPLACE "GLIBC_" "" version "${name}")
        if(version VERSION_GREATER glibc_needed)
            set(glibc_needed "${version}")
            set(glibc_needed_by "${file}")
        endif()
    endforeach()
endforeach()
message(STATUS "The AppImage needs glibc ${glibc_needed} (${glibc_needed_by})")
if(glibc_needed VERSION_GREATER CPACK_MOV_GLIBC_MAX)
    message(
        FATAL_ERROR
        "${glibc_needed_by} needs glibc ${glibc_needed}, above the supported floor ${CPACK_MOV_GLIBC_MAX}: "
        "build the AppImage in the Ubuntu 22.04 image (MOV_DEV_IMAGE=appimage)"
    )
endif()

# libstdc++: the hook was configured from the application's binaries; the
# libraries linuxdeploy added must not need more.
mov_required_stdcxx("${appdir}/usr" stdcxx_after)
if(NOT stdcxx_after STREQUAL stdcxx_required)
    message(FATAL_ERROR "Bundled libraries need ${stdcxx_after} from libstdc++, the hook checks ${stdcxx_required}")
endif()

set(CPACK_EXTERNAL_BUILT_PACKAGES "${output}")
