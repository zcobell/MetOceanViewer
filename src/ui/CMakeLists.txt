# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# The Qt Quick shell:
#   mov_ui          startup code and the --self-test, shared by the app and
#                   the Qt tests
#   mov_ui_qml      QML module "MetOceanViewer" (qml/)
#   metoceanviewer  the application
# qt_standard_project_setup() runs in the top-level CMakeLists.txt, next to
# find_package(Qt6). How the application is packaged: cmake/Packaging.cmake.

add_subdirectory(qml)

# mov_ui_qmlplugin, not just mov_ui_qml: the module is a static QML plugin, and
# linking the plugin target pulls in Qt's generated initializer, which
# registers the module so that loadFromModule() finds it without a
# Q_IMPORT_QML_PLUGIN in main(). QMapLibre::Core is for
# QMapLibre::supportedRendererType(); the map itself is the geoservices plugin,
# loaded at run time (mov_stage_maplibre_runtime). mov::io is for the PROJ
# data directory and the self-test.
mov_add_module(
    ui
    SOURCES startup.cpp self_test.cpp
    PUBLIC_LINK Qt6::Gui Qt6::Qml
    PRIVATE_LINK mov::core mov::io Qt6::Quick Qt6::Location QMapLibre::Core mov_ui_qmlplugin
)
# Where the PROJ database is, relative to the executable (cmake/Packaging.cmake).
target_compile_definitions(mov_ui PRIVATE "MOV_APP_PROJ_DATA_DIR=\"${MOV_APP_PROJ_DATA_DIR}\"")

qt_add_executable(metoceanviewer WIN32 MACOSX_BUNDLE main.cpp)
target_link_libraries(metoceanviewer PRIVATE mov::ui mov::options mov::warnings)
mov_enable_static_analysis(metoceanviewer)
mov_enforce_layer(metoceanviewer ui)
mov_set_app_metadata(metoceanviewer)
mov_stage_maplibre_runtime(metoceanviewer)
mov_stage_projection_data(metoceanviewer)
mov_install_app(metoceanviewer)
