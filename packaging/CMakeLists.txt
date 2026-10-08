# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# CPack: the release packages of the install tree that mov_install_app()
# (cmake/Packaging.cmake) describes. `cpack` (or the package-* presets) makes
#   macOS    DragNDrop  MetOceanViewer-<version>-macos-arm64.dmg
#   Windows  INNOSETUP  MetOceanViewer-<version>-windows-x64.exe (installer)
#            ZIP        MetOceanViewer-<version>-windows-x64-portable.zip
#   Linux    External   MetOceanViewer-<version>-linux-x86_64.AppImage
#                       (linuxdeploy: packaging/linux/appimage.cmake)
# The release workflow writes their checksums. Signing hooks are described in
# docs/packaging.md. Variables for the packaging scripts are CPACK_MOV_*.

set(CPACK_PACKAGE_NAME "${MOV_APP_NAME}")
set(CPACK_PACKAGE_VENDOR "Zach Cobell")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${MOV_APP_NAME}") # one install, upgraded in place
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES ON)
# Values in this file are written to CPackConfig.cmake verbatim.
set(CPACK_VERBATIM_VARIABLES ON)
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cpack-project-config.cmake")
set(CPACK_MOV_PACKAGING_DIR "${MOV_PACKAGING_DIR}")

# Pins read from tools/versions.env below: a change there reconfigures.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tools/versions.env")

# The architecture in the package names.
if(APPLE)
    set(arch "${CMAKE_OSX_ARCHITECTURES}")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64)$")
    if(WIN32)
        set(arch x64)
    else()
        set(arch x86_64)
    endif()
else()
    message(FATAL_ERROR "No package name for the architecture ${CMAKE_SYSTEM_PROCESSOR}")
endif()
if(NOT arch MATCHES "^(arm64|x64|x86_64)$")
    message(FATAL_ERROR "Packages are built for one architecture: arm64 (macOS) or x86-64, not '${arch}'")
endif()

if(APPLE)
    set(CPACK_GENERATOR DragNDrop)
    set(CPACK_SYSTEM_NAME macos-${arch})
    set(CPACK_DMG_VOLUME_NAME "${MOV_APP_NAME}")
    set(CPACK_PACKAGE_ICON "${MOV_PACKAGING_DIR}/icons/metoceanviewer.icns") # the volume icon
    set(CPACK_DMG_FORMAT UDZO)
    set(CPACK_DMG_FILESYSTEM APFS)
    set(CPACK_DMG_BACKGROUND_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/macos/dmg-background.png")
    set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/macos/dmg-setup.applescript")
    # The bundle is checked and signed before it goes into the image, the
    # image signed and notarized after it is made (packaging/macos/sign.sh).
    set(CPACK_MOV_MACOS_MINIMUM "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    set(CPACK_PRE_BUILD_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/macos/cpack-sign-app.cmake")
    set(CPACK_POST_BUILD_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/macos/cpack-sign-dmg.cmake")
elseif(WIN32)
    set(CPACK_GENERATOR INNOSETUP ZIP)
    set(CPACK_SYSTEM_NAME windows-${arch})
    set(CPACK_PACKAGE_EXECUTABLES metoceanviewer "${MOV_APP_NAME}")
    set(CPACK_INNOSETUP_ARCHITECTURE x64)
    set(CPACK_INNOSETUP_USE_MODERN_WIZARD ON)
    set(CPACK_INNOSETUP_ICON_FILE "${MOV_PACKAGING_DIR}/icons/metoceanviewer.ico")
    set(CPACK_INNOSETUP_RUN_EXECUTABLES metoceanviewer)
    set(CPACK_INNOSETUP_EXTRA_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/windows/installer.iss")
    # A fixed AppId, so a new version upgrades the old one in place.
    set(CPACK_INNOSETUP_SETUP_AppId "${MOV_APP_ID}")
    set(CPACK_INNOSETUP_SETUP_ChangesAssociations ON)
    # Windows 10 22H2 (build 19045) or later (plan §6.24).
    set(CPACK_INNOSETUP_SETUP_MinVersion "10.0.19045")
    # Administrative (Program Files) by default; per-user on request.
    set(CPACK_INNOSETUP_SETUP_PrivilegesRequiredOverridesAllowed "dialog commandline")
    set(CPACK_INNOSETUP_SETUP_UninstallDisplayIcon "{app}\\bin\\metoceanviewer.exe")
else()
    set(CPACK_GENERATOR External)
    set(CPACK_SYSTEM_NAME linux-${arch})
    set(CPACK_EXTERNAL_ENABLE_STAGING ON)
    set(CPACK_EXTERNAL_PACKAGE_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/linux/appimage.cmake")
    # The staging directory is the AppDir; its usr/ is the install prefix.
    set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
    set(CPACK_MOV_APP_ID "${MOV_APP_ID}")
    get_property(CPACK_MOV_EXECUTABLE GLOBAL PROPERTY MOV_INSTALLED_EXECUTABLE) # bin/metoceanviewer
    set(CPACK_MOV_TOOLS_DIR "${PROJECT_BINARY_DIR}/_packaging-tools")
    set(CPACK_MOV_QML_SOURCES "${PROJECT_SOURCE_DIR}/src/ui/qml")

    # From tools/versions.env: linuxdeploy, its Qt plugin and the AppImage
    # runtime (release tag and SHA-256), and the floors of the oldest
    # supported system, Ubuntu 22.04 (plan §6.24), which the script checks:
    # glibc, and libgcc_s's GCC_ symbol version.
    file(STRINGS "${PROJECT_SOURCE_DIR}/tools/versions.env" pins REGEX "^(LINUXDEPLOY_|APPIMAGE_)[A-Z0-9_]+=")
    foreach(pin IN LISTS pins)
        string(REGEX MATCH "^([A-Z0-9_]+)=(.*)$" unused "${pin}")
        set(CPACK_MOV_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
    endforeach()
    foreach(
        pin
        LINUXDEPLOY_VERSION
        LINUXDEPLOY_SHA256
        LINUXDEPLOY_PLUGIN_QT_VERSION
        LINUXDEPLOY_PLUGIN_QT_SHA256
        LINUXDEPLOY_APPIMAGE_RUNTIME_VERSION
        LINUXDEPLOY_APPIMAGE_RUNTIME_SHA256
        APPIMAGE_GLIBC_MAX
        APPIMAGE_LIBGCC_S_MAX
        APPIMAGE_LIBSTDCXX_URL
        APPIMAGE_LIBSTDCXX_SHA256
    )
        if(NOT CPACK_MOV_${pin})
            message(FATAL_ERROR "${pin} is missing from tools/versions.env")
        endif()
    endforeach()
    set(CPACK_MOV_GLIBC_MAX "${CPACK_MOV_APPIMAGE_GLIBC_MAX}")
    set(CPACK_MOV_LIBGCC_S_MAX "${CPACK_MOV_APPIMAGE_LIBGCC_S_MAX}")

    # Qt, for linuxdeploy's Qt plugin.
    set(CPACK_MOV_QT_PREFIX "${QT6_INSTALL_PREFIX}")
    set(CPACK_MOV_QT_LIBDIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBS}")
    set(CPACK_MOV_QMAKE "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}/qmake")
    set(CPACK_MOV_QT_PLUGINS_DIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}")

    # The libstdc++ the AppImage carries for systems whose own is older
    # (packaging/linux/apprun-hooks/mov-runtime.sh.in): a released one, of a
    # GCC no older than the compiler, whose headers the code was compiled with.
    if(NOT CPACK_MOV_APPIMAGE_LIBSTDCXX_URL MATCHES "/libstdcxx-([0-9]+\\.[0-9]+\\.[0-9]+)-[^/]*$")
        message(FATAL_ERROR "No GCC version in APPIMAGE_LIBSTDCXX_URL (${CPACK_MOV_APPIMAGE_LIBSTDCXX_URL})")
    endif()
    set(CPACK_MOV_LIBSTDCXX_GCC_VERSION "${CMAKE_MATCH_1}")
    if(
        CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
        AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER CPACK_MOV_LIBSTDCXX_GCC_VERSION
    )
        message(
            FATAL_ERROR
            "The bundled libstdc++ (GCC ${CPACK_MOV_LIBSTDCXX_GCC_VERSION}) is older than the compiler "
            "(GCC ${CMAKE_CXX_COMPILER_VERSION}): binaries may need symbols it lacks"
        )
    endif()
endif()

set(CPACK_PACKAGE_FILE_NAME "${MOV_APP_NAME}-${PROJECT_VERSION}-${CPACK_SYSTEM_NAME}")
set(CPACK_PACKAGE_DIRECTORY "${PROJECT_BINARY_DIR}/packages")

include(CPack)
