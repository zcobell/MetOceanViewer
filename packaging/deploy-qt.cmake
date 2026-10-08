# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# Qt deployment of the installed application (macOS: macdeployqt, Windows:
# windeployqt), run at install time by the script mov_install_app() generates
# with qt_generate_deploy_script(), which defines Qt's deploy API and:
#   MOV_DEPLOY_TARGET      the executable target (its QML imports were recorded
#                          at configure time)
#   MOV_DEPLOY_EXECUTABLE  the installed executable (the .app on macOS),
#                          relative to the install prefix
#   MOV_DEPLOY_LIBDIR      where the QMapLibre libraries were installed
#   MOV_DEPLOY_PLUGINDIR   the Qt plugin directory of the install
# Relative paths are relative to QT_DEPLOY_PREFIX, the deploy tool's working
# directory.

# The QML modules the application imports (QtQuick, QtQuick.Controls,
# QtLocation, ...) and their plugins.
qt_deploy_qml_imports(TARGET "${MOV_DEPLOY_TARGET}" PLUGINS_FOUND qml_plugins)

# The deploy tool must also scan the QMapLibre libraries and the maplibre
# plugin, which the executable does not link: they need Qt modules it does not
# use itself (Qt Sql for the tile cache, Qt Location's private API).
file(GLOB maplibre_libraries RELATIVE "${QT_DEPLOY_PREFIX}" "${QT_DEPLOY_PREFIX}/${MOV_DEPLOY_LIBDIR}/*QMapLibre*")
set(scan_libraries "")
foreach(library IN LISTS maplibre_libraries)
    if(library MATCHES "/([^/]+)\\.framework$")
        # A framework's binary is the file named after it.
        list(APPEND scan_libraries "${library}/${CMAKE_MATCH_1}")
    elseif(library MATCHES "\\.(dll|dylib)$" AND NOT IS_SYMLINK "${QT_DEPLOY_PREFIX}/${library}")
        list(APPEND scan_libraries "${library}")
    endif()
endforeach()
file(
    GLOB maplibre_plugin
    RELATIVE "${QT_DEPLOY_PREFIX}"
    "${QT_DEPLOY_PREFIX}/${MOV_DEPLOY_PLUGINDIR}/geoservices/*maplibre*"
)
if(NOT scan_libraries OR NOT maplibre_plugin)
    message(FATAL_ERROR "QMapLibre is not installed under ${MOV_DEPLOY_LIBDIR} and ${MOV_DEPLOY_PLUGINDIR}/geoservices")
endif()

qt_deploy_runtime_dependencies(
    EXECUTABLE "${MOV_DEPLOY_EXECUTABLE}"
    ADDITIONAL_LIBRARIES ${scan_libraries}
    ADDITIONAL_MODULES ${qml_plugins} ${maplibre_plugin}
    GENERATE_QT_CONF
    # The application has no translations yet, so Qt's own would be half a
    # translation. The MSVC runtime is installed app-local by CMake
    # (InstallRequiredSystemLibraries), not as vc_redist.exe.
    NO_TRANSLATIONS
    NO_COMPILER_RUNTIME
)

# The deploy tools copy every SQL driver; MapLibre needs SQLite only, and the
# others link client libraries (libpq, ODBC, ...) that are not shipped.
file(GLOB sql_drivers "${QT_DEPLOY_PREFIX}/${MOV_DEPLOY_PLUGINDIR}/sqldrivers/*")
foreach(driver IN LISTS sql_drivers)
    get_filename_component(name "${driver}" NAME)
    if(NOT name MATCHES "^(lib)?qsqlite\\.")
        message(STATUS "Removing unused SQL driver ${name}")
        file(REMOVE "${driver}")
    endif()
endforeach()

# Windows: MapLibre renders with OpenGL, and opengl32sw.dll (Mesa llvmpipe) is
# Qt's fallback where the GPU driver has no OpenGL 2. windeployqt copies it
# unless told not to (never pass --no-opengl-sw); say so if Qt lacks it.
if(CMAKE_HOST_WIN32 AND NOT EXISTS "${QT_DEPLOY_PREFIX}/${MOV_DEPLOY_LIBDIR}/opengl32sw.dll")
    message(WARNING "opengl32sw.dll was not deployed: machines without OpenGL 2 drivers cannot draw the map")
endif()
