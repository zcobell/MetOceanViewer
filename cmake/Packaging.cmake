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
#   mov_install_app(<target>)            install rules: the application,
#                                        MapLibre, proj.db, notices, Linux
#                                        desktop integration, and on macOS and
#                                        Windows the Qt deployment step.
#
# packaging/CMakeLists.txt turns the install tree into packages (CPack).

set(MOV_APP_ID io.github.zcobell.metoceanviewer) # plan §6.19
set(MOV_APP_NAME MetOceanViewer)
set(MOV_PACKAGING_DIR "${PROJECT_SOURCE_DIR}/packaging")

# The installed layout, relative to the install prefix. MOV_APP_PROJ_DATA_DIR
# is the same PROJ data directory relative to the executable's directory:
# startup.cpp compiles it in, and mov_stage_projection_data reproduces it in
# the build tree.
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
    if(NOT MOV_PROJ_DB)
        message(WARNING "proj.db not found (MOV_PROJ_DB): ${target} will not find the PROJ database")
        return()
    endif()
    set(dir "$<TARGET_FILE_DIR:${target}>/${MOV_APP_PROJ_DATA_DIR}")
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MOV_PROJ_DB}" "${dir}"
        VERBATIM
    )
endfunction()

# The license files vcpkg installed for the ports the application ships
# (vcpkg_installed/<triplet>/share/<port>/copyright), except build-only ones.
function(_mov_vcpkg_notices out_var)
    set(share "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share")
    if(NOT VCPKG_INSTALLED_DIR OR NOT IS_DIRECTORY "${share}")
        message(WARNING "No vcpkg install tree: the package will lack the third-party notices")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    file(GLOB notices "${share}/*/copyright")
    list(FILTER notices EXCLUDE REGEX "/share/(catch2|vcpkg-[^/]*)/copyright$")
    set(${out_var} "${notices}" PARENT_SCOPE)
endfunction()

# Linux desktop integration: the .desktop entry, the .mvs MIME type and the
# icons, all named after the application id. linuxdeploy reads the first and
# the icons; the AppImage carries all three for desktop integrators.
function(_mov_install_linux_desktop_files)
    install(FILES "${MOV_PACKAGING_DIR}/linux/${MOV_APP_ID}.desktop" DESTINATION share/applications)
    install(FILES "${MOV_PACKAGING_DIR}/linux/${MOV_APP_ID}.xml" DESTINATION share/mime/packages)
    file(GLOB pngs RELATIVE "${MOV_PACKAGING_DIR}/icons/hicolor" "${MOV_PACKAGING_DIR}/icons/hicolor/*.png")
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
# and the MSVC runtime. Qt's DLLs are windeployqt's (deploy-qt.cmake).
function(_mov_install_windows_runtime)
    install(
        RUNTIME_DEPENDENCY_SET mov_app_runtime
        PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-" "^[Qq]t6"
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
    if(NOT MOV_PROJ_DB)
        message(FATAL_ERROR "proj.db was not found under PROJ_DIR (${PROJ_DIR}); the package must ship it")
    endif()

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

    # Notices: the application's license and every shipped port's (the
    # maplibre-native-qt port's covers MapLibre's vendored code).
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION ${MOV_INSTALL_DOCDIR})
    _mov_vcpkg_notices(notices)
    foreach(notice IN LISTS notices)
        get_filename_component(port "${notice}" DIRECTORY)
        get_filename_component(port "${port}" NAME)
        install(FILES "${notice}" DESTINATION "${MOV_INSTALL_DOCDIR}/third-party" RENAME "${port}.txt")
    endforeach()

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
set(MOV_DEPLOY_SYSTEM \"${CMAKE_SYSTEM_NAME}\")
set(MOV_DEPLOY_EXECUTABLE \"${installed_executable}\")
set(MOV_DEPLOY_LIBDIR \"${MOV_INSTALL_LIBDIR}\")
set(MOV_DEPLOY_PLUGINDIR \"${MOV_INSTALL_PLUGINDIR}\")
include(\"${MOV_PACKAGING_DIR}/deploy-qt.cmake\")
"
        )
        install(SCRIPT "${deploy_script}")
    endif()
endfunction()
