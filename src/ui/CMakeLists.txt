# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# The Qt Quick shell:
#   mov_ui          startup code and the --self-test, shared by the app and
#                   the Qt tests
#   mov_ui_qml      QML module "MetOceanViewer" (qml/)
#   metoceanviewer  the application
# qt_standard_project_setup() runs in the top-level CMakeLists.txt, next to
# find_package(Qt6). How the application is packaged: cmake/Packaging.cmake.

# The QML module's URI: qml/CMakeLists.txt and app_identity.hpp.
set(MOV_QML_MODULE_URI MetOceanViewer)
add_subdirectory(qml)

# mov_ui_qmlplugin, not just mov_ui_qml: the module is a static QML plugin, and
# linking the plugin target pulls in Qt's generated initializer, which
# registers the module so that loadFromModule() finds it without a
# Q_IMPORT_QML_PLUGIN in main(). QMapLibre::Core is for
# QMapLibre::supportedRendererType(); the map itself is the geoservices plugin,
# loaded at run time (mov_stage_maplibre_runtime). mov::io is for the PROJ
# data directory and the self-test, which also checks Qt Network's TLS and
# Qt Sql's SQLite driver.
mov_add_module(
    ui
    SOURCES startup.cpp self_test.cpp
    PUBLIC_LINK Qt6::Gui Qt6::Qml
    PRIVATE_LINK mov::core mov::io Qt6::Quick Qt6::Location Qt6::Network Qt6::Sql QMapLibre::Core mov_ui_qmlplugin
)
# mov/ui/app_identity.hpp: the identity and layout values the packages use too
# (cmake/Packaging.cmake), in one place.
configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/app_identity.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/mov/ui/app_identity.hpp"
    @ONLY
)
target_include_directories(mov_ui PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/generated")

qt_add_executable(metoceanviewer WIN32 MACOSX_BUNDLE main.cpp)
target_link_libraries(metoceanviewer PRIVATE mov::ui mov::options mov::warnings)
mov_enable_static_analysis(metoceanviewer)
mov_enforce_layer(metoceanviewer ui)
mov_set_app_metadata(metoceanviewer)
mov_stage_maplibre_runtime(metoceanviewer)
mov_stage_projection_data(metoceanviewer)
mov_split_debug_info(metoceanviewer)
mov_install_app(metoceanviewer)
