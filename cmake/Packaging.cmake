# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# The application as it is shipped (docs/packaging.md). Included by the
# top-level CMakeLists.txt with the Qt layers; used by src/ui/CMakeLists.txt:
#
#   mov_set_app_metadata(<target>)       metadata compiled into the executable:
#                                        Info.plist and the .icns (macOS), the
#                                        VERSIONINFO, icon and application
#                                        manifest (Windows). Every version
#                                        comes from project(VERSION).
#   mov_stage_projection_data(<target>)  copies proj.db to where the
#                                        application looks for it, so a build
#                                        tree behaves like an install.
#   mov_split_debug_info(<target>)       with MOV_SPLIT_DEBUG_INFO (package
#                                        presets): the debug information
#                                        beside the binary, for the symbol
#                                        artifacts; the package ships stripped.
#   mov_install_app(<target>)            install rules: the application,
#                                        MapLibre, proj.db, notices, Linux
#                                        desktop integration, and on macOS and
#                                        Windows the Qt deployment step.
#
# packaging/CMakeLists.txt turns the install tree into packages (CPack).
# Values other files repeat are listed in docs/packaging.md, "Keep in sync".

# The bundle id; also the desktop file name and the installer's AppId.
set(MOV_APP_ID io.github.zcobell.metoceanviewer)
set(MOV_APP_NAME MetOceanViewer)
set(MOV_PACKAGING_DIR "${PROJECT_SOURCE_DIR}/packaging")

option(
    MOV_SPLIT_DEBUG_INFO
    "Keep the application's debug information in a separate file (.debug, .dSYM; MSVC's .pdb is always separate)"
    OFF
)

# The installed layout, relative to the install prefix. MOV_APP_PROJ_DATA_DIR
# is the same PROJ data directory relative to the executable's directory:
# app_identity.hpp carries it to startup.cpp, and mov_stage_projection_data
# reproduces it in the build tree.
if(APPLE)
    set(MOV_APP_BUNDLE "${MOV_APP_NAME}.app")
    set(MOV_INSTALL_LIBDIR "${MOV_APP_BUNDLE}/Contents/Frameworks")
    set(MOV_INSTALL_PLUGINDIR "${MOV_APP_BUNDLE}/Contents/PlugIns")
    set(MOV_INSTALL_DATADIR "${MOV_APP_BUNDLE}/Contents/Resources")
    set(MOV_INSTALL_DOCDIR "${MOV_APP_BUNDLE}/Contents/Resources/licenses")
    set(MOV_APP_PROJ_DATA_DIR "../Resources/proj") # from Contents/MacOS
else()
    set(MOV_INSTALL_BINDIR bin)
    if(WIN32)
        set(MOV_INSTALL_LIBDIR bin) # DLLs beside the executable
    else()
        set(MOV_INSTALL_LIBDIR lib) # the RUNPATH below and linuxdeploy's usr/lib
    endif()
    # Qt's deploy step (Windows) and linuxdeploy (Linux) both use plugins/.
    set(MOV_INSTALL_PLUGINDIR plugins)
    set(MOV_INSTALL_DATADIR share/metoceanviewer)
    set(MOV_INSTALL_DOCDIR share/doc/metoceanviewer)
    set(MOV_APP_PROJ_DATA_DIR "../share/metoceanviewer/proj") # from bin/
endif()

# vcpkg ports that are not part of the shipped binaries: build tools and the
# test framework. Every other installed port is linked into them, so its
# license ships (_mov_vcpkg_notices).
set(MOV_NON_RUNTIME_PORTS catch2 vcpkg-cmake vcpkg-cmake-config)

# proj.db of the PROJ install (MOV_PROJ_DB, src/io/CMakeLists.txt). A package
# without it would fall back to the copy built into a static PROJ, which
# mov::io refuses once the packaged directory is set (projection.hpp).
function(_mov_require_proj_db)
    if(NOT MOV_PROJ_DB)
        message(FATAL_ERROR "proj.db was not found under PROJ_DIR (${PROJ_DIR}); the application must ship it")
    endif()
endfunction()

# mov_set_app_metadata(<target>)
function(mov_set_app_metadata target)
    set(out "${CMAKE_CURRENT_BINARY_DIR}/app-metadata")
    if(WIN32)
        # rc.exe finds the icon beside the .rc file.
        configure_file("${MOV_PACKAGING_DIR}/icons/metoceanviewer.ico" "${out}/metoceanviewer.ico" COPYONLY)
        configure_file("${MOV_PACKAGING_DIR}/windows/metoceanviewer.rc.in" "${out}/metoceanviewer.rc" @ONLY)
        # MSVC's linker merges a .manifest source into the embedded manifest.
        configure_file("${MOV_PACKAGING_DIR}/windows/metoceanviewer.manifest.in" "${out}/metoceanviewer.manifest" @ONLY)
        target_sources(${target} PRIVATE "${out}/metoceanviewer.rc" "${out}/metoceanviewer.manifest")
    elseif(APPLE)
        # LSMinimumSystemVersion is the deployment target the code is built
        # for (14.0, the oldest supported macOS, set by the macOS presets).
        if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
            message(FATAL_ERROR "Set CMAKE_OSX_DEPLOYMENT_TARGET (the ci-macos and package-macos presets do)")
        endif()
        set(MOV_MACOS_MINIMUM "${CMAKE_OSX_DEPLOYMENT_TARGET}")
        set(icon "${MOV_PACKAGING_DIR}/icons/metoceanviewer.icns")
        set_source_files_properties("${icon}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        target_sources(${target} PRIVATE "${icon}")
        # Fully substituted here; CMake's own pass over MACOSX_BUNDLE_INFO_PLIST
        # then finds nothing left to replace.
        configure_file("${MOV_PACKAGING_DIR}/macos/Info.plist.in" "${out}/Info.plist" @ONLY)
        set_target_properties(
            ${target}
            PROPERTIES OUTPUT_NAME "${MOV_APP_NAME}" MACOSX_BUNDLE_INFO_PLIST "${out}/Info.plist"
        )
    endif()
endfunction()

# mov_stage_projection_data(<target>)
#
# Copies proj.db to MOV_APP_PROJ_DATA_DIR relative to <target>'s executable,
# where mov::ui::configure_projection_data() looks for it, exactly as in an
# installed package.
function(mov_stage_projection_data target)
    _mov_require_proj_db()
    set(dir "$<TARGET_FILE_DIR:${target}>/${MOV_APP_PROJ_DATA_DIR}")
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MOV_PROJ_DB}" "${dir}"
        VERBATIM
    )
endfunction()

# mov_split_debug_info(<target>)
#
# With MOV_SPLIT_DEBUG_INFO: Linux, <file>.debug plus a .gnu_debuglink in the
# binary (install --strip, CPACK_STRIP_FILES, then removes the rest); macOS,
# <bundle>.dSYM. MSVC writes the .pdb anyway (ProgramDatabase in the
# package-windows preset). The package workflow uploads them as artifacts.
function(mov_split_debug_info target)
    if(NOT MOV_SPLIT_DEBUG_INFO)
        return()
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_custom_command(
            TARGET ${target}
            POST_BUILD
            COMMAND "${CMAKE_OBJCOPY}" --only-keep-debug "$<TARGET_FILE:${target}>" "$<TARGET_FILE:${target}>.debug"
            COMMAND "${CMAKE_OBJCOPY}" "--add-gnu-debuglink=$<TARGET_FILE:${target}>.debug" "$<TARGET_FILE:${target}>"
            VERBATIM
        )
    elseif(APPLE)
        find_program(MOV_DSYMUTIL dsymutil REQUIRED)
        add_custom_command(
            TARGET ${target}
            POST_BUILD
            COMMAND "${MOV_DSYMUTIL}" "$<TARGET_FILE:${target}>" -o "$<TARGET_BUNDLE_DIR:${target}>.dSYM"
            VERBATIM
        )
    endif()
endfunction()

# The license files vcpkg installed for the ports the application ships
# (vcpkg_installed/<triplet>/share/<port>/copyright).
function(_mov_vcpkg_notices out_var)
    set(share "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share")
    if(NOT VCPKG_INSTALLED_DIR OR NOT IS_DIRECTORY "${share}")
        message(FATAL_ERROR "No vcpkg install tree (${share}): the package needs the ports' notices")
    endif()
    file(GLOB notices "${share}/*/copyright")
    list(JOIN MOV_NON_RUNTIME_PORTS "|" excluded)
    list(FILTER notices EXCLUDE REGEX "/share/(${excluded})/copyright$")
    set(${out_var} "${notices}" PARENT_SCOPE)
endfunction()

# Linux desktop integration: the .desktop entry, the .mvs MIME type and the
# icons, all named after the application id. linuxdeploy reads the first and
# the icons; the AppImage carries all three for desktop integrators.
function(_mov_install_linux_desktop_files)
    install(FILES "${MOV_PACKAGING_DIR}/linux/${MOV_APP_ID}.desktop" DESTINATION share/applications)
    install(FILES "${MOV_PACKAGING_DIR}/linux/${MOV_APP_ID}.xml" DESTINATION share/mime/packages)
    file(
        GLOB pngs
        CONFIGURE_DEPENDS
        RELATIVE "${MOV_PACKAGING_DIR}/icons/hicolor"
        "${MOV_PACKAGING_DIR}/icons/hicolor/*.png"
    )
    foreach(png IN LISTS pngs)
        string(REGEX REPLACE "\\.png$" "" size "${png}")
        install(
            FILES "${MOV_PACKAGING_DIR}/icons/hicolor/${png}"
            DESTINATION "share/icons/hicolor/${size}/apps"
            RENAME "${MOV_APP_ID}.png"
        )
    endforeach()
    install(
        FILES "${PROJECT_SOURCE_DIR}/src/ui/qml/images/app-icon.svg"
        DESTINATION share/icons/hicolor/scalable/apps
        RENAME "${MOV_APP_ID}.svg"
    )
endfunction()

# Windows: the DLLs of the vcpkg ports (netCDF, HDF5, PROJ, SQLite, zlib, ...)
# and the MSVC runtime. Qt's DLLs are windeployqt's (deploy-qt.cmake), the
# QMapLibre ones mov_install_app's.
function(_mov_install_windows_runtime)
    # MapLibre is release-only (cmake/MapLibre.cmake): an install of another
    # configuration would mix debug and release Qt and CRTs.
    install(
        CODE
            "if(CMAKE_INSTALL_CONFIG_NAME MATCHES \"^Debug$\")
    message(FATAL_ERROR \"Install a release-type configuration (Release, RelWithDebInfo)\")
endif()"
    )
    install(
        RUNTIME_DEPENDENCY_SET mov_app_runtime
        PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-" "^[Qq]t6" "^QMapLibre"
        POST_EXCLUDE_REGEXES "^[A-Za-z]:[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\]"
        DIRECTORIES "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
        RUNTIME DESTINATION ${MOV_INSTALL_BINDIR}
    )
    # App-local MSVC runtime (msvcp140.dll, vcruntime140*.dll), which Microsoft
    # permits; Windows 10+ has the UCRT. No vc_redist.exe to run.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${MOV_INSTALL_BINDIR})
    set(CMAKE_INSTALL_UCRT_LIBRARIES OFF)
    include(InstallRequiredSystemLibraries)
endfunction()

# mov_install_app(<target>)
function(mov_install_app target)
    _mov_require_proj_db()

    if(APPLE)
        set_target_properties(${target} PROPERTIES INSTALL_RPATH "@executable_path/../Frameworks")
        install(TARGETS ${target} BUNDLE DESTINATION .)
        set(installed_executable "${MOV_APP_BUNDLE}")
    elseif(WIN32)
        install(TARGETS ${target} RUNTIME_DEPENDENCY_SET mov_app_runtime RUNTIME DESTINATION ${MOV_INSTALL_BINDIR})
        set(installed_executable "${MOV_INSTALL_BINDIR}/$<TARGET_FILE_NAME:${target}>")
        _mov_install_windows_runtime()
    else()
        set_target_properties(${target} PROPERTIES INSTALL_RPATH "$ORIGIN/../${MOV_INSTALL_LIBDIR}")
        install(TARGETS ${target} RUNTIME DESTINATION ${MOV_INSTALL_BINDIR})
        _mov_install_linux_desktop_files()
        # For the AppImage script (packaging/CMakeLists.txt).
        set_property(GLOBAL PROPERTY MOV_INSTALLED_EXECUTABLE "${MOV_INSTALL_BINDIR}/${target}")
    endif()

    # MapLibre: the libraries (frameworks on macOS) and the "maplibre"
    # geoservices plugin, where Qt looks for plugins (qt.conf: plugins/, or
    # the bundle's PlugIns/).
    install(
        IMPORTED_RUNTIME_ARTIFACTS ${MOV_MAPLIBRE_RUNTIME_LIBRARIES}
        LIBRARY DESTINATION ${MOV_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${MOV_INSTALL_LIBDIR}
        FRAMEWORK DESTINATION ${MOV_INSTALL_LIBDIR}
    )
    install(
        IMPORTED_RUNTIME_ARTIFACTS QMapLibre::PluginGeoServices
        LIBRARY DESTINATION "${MOV_INSTALL_PLUGINDIR}/geoservices"
    )

    install(FILES "${MOV_PROJ_DB}" DESTINATION "${MOV_INSTALL_DATADIR}/proj")

    # Notices: the application's license, every shipped port's (the
    # maplibre-native-qt port's covers MapLibre's vendored code) and Qt's
    # (packaging/licenses/qt, tools/fetch_qt_licenses.py: Qt's binaries carry
    # none). The AppImage adds the Ubuntu packages' (appimage.cmake).
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION ${MOV_INSTALL_DOCDIR})
    _mov_vcpkg_notices(notices)
    foreach(notice IN LISTS notices)
        get_filename_component(port "${notice}" DIRECTORY)
        get_filename_component(port "${port}" NAME)
        install(FILES "${notice}" DESTINATION "${MOV_INSTALL_DOCDIR}/third-party" RENAME "${port}.txt")
    endforeach()
    install(DIRECTORY "${MOV_PACKAGING_DIR}/licenses/qt/" DESTINATION "${MOV_INSTALL_DOCDIR}/third-party/qt")

    # Qt itself: macdeployqt and windeployqt through Qt's deploy API, run at
    # install time after the files above are in place. On Linux, linuxdeploy
    # and its Qt plugin do this when the AppImage is made
    # (packaging/linux/appimage.cmake), so the install tree there is not
    # self-contained.
    if(APPLE OR WIN32)
        qt_generate_deploy_script(
            TARGET ${target}
            OUTPUT_SCRIPT deploy_script
            CONTENT
                "
set(MOV_DEPLOY_TARGET \"${target}\")
set(MOV_DEPLOY_EXECUTABLE \"${installed_executable}\")
set(MOV_DEPLOY_LIBDIR \"${MOV_INSTALL_LIBDIR}\")
set(MOV_DEPLOY_PLUGINDIR \"${MOV_INSTALL_PLUGINDIR}\")
include(\"${MOV_PACKAGING_DIR}/deploy-qt.cmake\")
"
        )
        install(SCRIPT "${deploy_script}")
    endif()
endfunction()
