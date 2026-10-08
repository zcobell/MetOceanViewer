# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# CPACK_EXTERNAL_PACKAGE_SCRIPT of the Linux package: turns the staged install
# tree into an AppImage with linuxdeploy and its Qt plugin (docs/packaging.md).
#
# CPack has installed the project into CPACK_TEMPORARY_DIRECTORY with the
# prefix /usr, so that directory is the AppDir. The steps, in this order
# because each needs the one before:
#   1. download linuxdeploy, its Qt plugin and the AppImage runtime (pinned
#      in tools/versions.env, SHA-256 verified) into CPACK_MOV_TOOLS_DIR, once;
#   2. give the Qt plugin a view of Qt without the plugins whose libraries
#      are not installed (SQL drivers other than SQLite, NMEA positioning);
#   3. deploy: linuxdeploy with the Qt plugin bundles Qt, the QML imports and
#      the system libraries outside its exclude list (no AppImage yet, so the
#      next steps see the complete AppDir);
#   4. check the complete AppDir: the glibc floor (plan §6.24), no
#      GLIBC_PRIVATE, no absolute RUNPATH, libgcc_s's floor, and a license
#      for every Ubuntu library linuxdeploy bundled;
#   5. bundle libstdc++ and the AppRun hook, configured with the symbol
#      versions the binaries just checked need; replace the Qt plugin's own
#      hook (it only sets a gtk2 platform theme, which Qt 6 does not have);
#   6. make the AppImage: linuxdeploy again, which rewrites AppRun to source
#      the hooks now present.
# Variables from packaging/CMakeLists.txt are named CPACK_MOV_*.

cmake_minimum_required(VERSION 4.4)

set(appdir "${CPACK_TEMPORARY_DIRECTORY}")
set(executable "${appdir}/usr/${CPACK_MOV_EXECUTABLE}")
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

# --- 3. Deploy ------------------------------------------------------------------

set(output "${CPACK_TOPLEVEL_DIRECTORY}/${CPACK_PACKAGE_FILE_NAME}.AppImage")
set(ENV{APPIMAGE_EXTRACT_AND_RUN} 1) # no FUSE in containers and CI
set(ENV{ARCH} x86_64)
set(ENV{LDAI_OUTPUT} "${output}")
set(ENV{OUTPUT} "${output}") # older appimage plugins
set(ENV{LDAI_RUNTIME_FILE} "${runtime}")
set(ENV{QMAKE} "${qt_view}/qmake")
# Where linuxdeploy resolves the Qt libraries (the executable's RUNPATH only
# covers usr/lib).
set(ENV{LD_LIBRARY_PATH} "${CPACK_MOV_QT_LIBDIR}")
# The QML the application compiles in, for qmlimportscanner.
set(ENV{QML_SOURCES_PATHS} "${CPACK_MOV_QML_SOURCES}")
# QIcon reads the SVG window icon through Qt Svg's plugins, which nothing
# links; offscreen runs the self-test headless.
set(ENV{EXTRA_QT_MODULES} "svg")
set(ENV{EXTRA_PLATFORM_PLUGINS} "libqoffscreen.so")
set(desktop_file "${appdir}/usr/share/applications/${CPACK_MOV_APP_ID}.desktop")
set(icon_file "${appdir}/usr/share/icons/hicolor/256x256/apps/${CPACK_MOV_APP_ID}.png")
execute_process(
    COMMAND
        "${linuxdeploy}" --appdir "${appdir}" --executable "${executable}" --desktop-file "${desktop_file}" --icon-file
        "${icon_file}" --deploy-deps-only "${appdir}/usr/plugins/geoservices" --plugin qt
    WORKING_DIRECTORY "${CPACK_TOPLEVEL_DIRECTORY}"
    COMMAND_ERROR_IS_FATAL ANY
)

# --- 4. Checks on the deployed AppDir ---------------------------------------------

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

# The highest <family>_x.y.z symbol version the ELF <files> reference (the
# version names in their dynamic string tables), or "" when none does;
# <where> names one file that needs it.
function(mov_highest_version family files out_var where_var)
    set(best "")
    set(where "")
    foreach(file IN LISTS files)
        file(STRINGS "${file}" names REGEX "^${family}_[0-9]+(\\.[0-9]+)*$")
        foreach(name IN LISTS names)
            string(REPLACE "${family}_" "" version "${name}")
            if(best STREQUAL "" OR version VERSION_GREATER best)
                set(best "${version}")
                set(where "${file}")
            endif()
        endforeach()
    endforeach()
    set(${out_var} "${best}" PARENT_SCOPE)
    set(${where_var} "${where}" PARENT_SCOPE)
endfunction()

mov_elf_files("${appdir}/usr" binaries)

# glibc: nothing may need a newer one than the oldest supported system's, nor
# glibc's private interface (which changes between releases).
mov_highest_version(GLIBC "${binaries}" glibc_needed glibc_needed_by)
message(STATUS "The AppImage needs glibc ${glibc_needed} (${glibc_needed_by})")
if(glibc_needed VERSION_GREATER CPACK_MOV_GLIBC_MAX)
    message(
        FATAL_ERROR
        "${glibc_needed_by} needs glibc ${glibc_needed}, above the supported floor ${CPACK_MOV_GLIBC_MAX}: "
        "build the AppImage in the Ubuntu 22.04 image (MOV_DEV_IMAGE=appimage)"
    )
endif()
foreach(file IN LISTS binaries)
    file(STRINGS "${file}" private REGEX "^GLIBC_PRIVATE$")
    if(private)
        message(FATAL_ERROR "${file} uses GLIBC_PRIVATE, which ties it to one glibc build")
    endif()
    # Every library must be found inside the AppDir: a RUNPATH or RPATH may
    # only be relative to the file ($ORIGIN).
    file(
        READ_ELF
        "${file}"
        RPATH
        rpath
        RUNPATH
        runpath
    )
    string(REPLACE ":" ";" search "${rpath}:${runpath}")
    foreach(entry IN LISTS search)
        if(entry AND NOT entry MATCHES "^\\$ORIGIN")
            message(FATAL_ERROR "${file} searches ${entry}: a path outside the AppDir")
        endif()
    endforeach()
endforeach()

# libgcc_s is not bundled: the binaries (and the bundled libstdc++) must not
# need more of it than the oldest supported system has (Ubuntu 22.04's).
mov_highest_version(GCC "${binaries};${CPACK_MOV_LIBSTDCXX}" gcc_s_needed gcc_s_needed_by)
if(gcc_s_needed VERSION_GREATER CPACK_MOV_LIBGCC_S_MAX)
    message(
        FATAL_ERROR
        "${gcc_s_needed_by} needs GCC_${gcc_s_needed} from libgcc_s, above Ubuntu 22.04's GCC_${CPACK_MOV_LIBGCC_S_MAX}"
    )
endif()

# Licenses: linuxdeploy copies /usr/share/doc/<package>/copyright of each
# Ubuntu library it bundles; make sure each has one. Qt's libraries
# (including its ICU) and QMapLibre come with their own notices
# (cmake/Packaging.cmake).
file(GLOB bundled LIST_DIRECTORIES false "${appdir}/usr/lib/*.so*")
list(FILTER bundled EXCLUDE REGEX "/lib(Qt6|icu|QMapLibre)[^/]*$")
foreach(library IN LISTS bundled)
    get_filename_component(name "${library}" NAME)
    execute_process(
        COMMAND dpkg-query --search "*/${name}"
        OUTPUT_VARIABLE owners
        RESULT_VARIABLE status
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT status EQUAL 0 OR NOT owners MATCHES "^([^:,]+)")
        message(FATAL_ERROR "No Ubuntu package owns ${name}: its license cannot be shipped")
    endif()
    set(package "${CMAKE_MATCH_1}")
    set(notice "${appdir}/usr/share/doc/${package}/copyright")
    if(NOT EXISTS "${notice}")
        if(NOT EXISTS "/usr/share/doc/${package}/copyright")
            message(FATAL_ERROR "${package} (${name}) has no /usr/share/doc/${package}/copyright")
        endif()
        message(STATUS "Adding the license of ${package} (${name})")
        file(MAKE_DIRECTORY "${appdir}/usr/share/doc/${package}")
        file(COPY_FILE "/usr/share/doc/${package}/copyright" "${notice}")
    endif()
endforeach()

# --- 5. libstdc++ and the AppRun hook -------------------------------------------

list(FILTER binaries EXCLUDE REGEX "/usr/optional/")
mov_highest_version(GLIBCXX "${binaries}" glibcxx unused)
mov_highest_version(CXXABI "${binaries}" cxxabi unused)
set(MOV_STDCXX_REQUIRED "GLIBCXX_${glibcxx} CXXABI_${cxxabi}")
execute_process(
    COMMAND dpkg-query --show --showformat=\${Version} libstdc++6
    OUTPUT_VARIABLE libstdcxx_package
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(
    STATUS
    "The binaries need ${MOV_STDCXX_REQUIRED}; bundling ${CPACK_MOV_LIBSTDCXX} (libstdc++6 ${libstdcxx_package})"
)
file(MAKE_DIRECTORY "${appdir}/usr/optional/libstdc++")
# Under its soname (libstdc++.so.6.0.35 -> libstdc++.so.6), which the loader
# looks for.
file(COPY_FILE "${CPACK_MOV_LIBSTDCXX}" "${appdir}/usr/optional/libstdc++/libstdc++.so.6")
file(REMOVE "${appdir}/apprun-hooks/linuxdeploy-plugin-qt-hook.sh")
configure_file(
    "${CPACK_MOV_PACKAGING_DIR}/linux/apprun-hooks/mov-runtime.sh.in"
    "${appdir}/apprun-hooks/mov-runtime.sh"
    @ONLY
)

# --- 6. The AppImage -------------------------------------------------------------

execute_process(
    COMMAND "${linuxdeploy}" --appdir "${appdir}" --output appimage
    WORKING_DIRECTORY "${CPACK_TOPLEVEL_DIRECTORY}"
    COMMAND_ERROR_IS_FATAL ANY
)
if(NOT EXISTS "${output}")
    message(FATAL_ERROR "linuxdeploy did not write ${output}")
endif()

set(CPACK_EXTERNAL_BUILT_PACKAGES "${output}")
