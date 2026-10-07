# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# The Qt Quick shell:
#   mov_ui          startup code shared by the app and the GUI tests
#   mov_ui_qml      QML module "MetOceanViewer" (qml/)
#   metoceanviewer  the application
# qt_standard_project_setup() runs in the top-level CMakeLists.txt, next to
# find_package(Qt6).

add_subdirectory(qml)

# mov_ui_qmlplugin, not just mov_ui_qml: the module is a static QML plugin, and
# linking the plugin target pulls in Qt's generated initializer, which
# registers the module so that loadFromModule() finds it without a
# Q_IMPORT_QML_PLUGIN in main(). QMapLibre::Core is for
# QMapLibre::supportedRendererType(); the map itself is the geoservices plugin,
# loaded at run time (mov_stage_maplibre_runtime).
mov_add_module(
    ui
    SOURCES startup.cpp
    PUBLIC_LINK Qt6::Gui Qt6::Qml
    PRIVATE_LINK mov::core Qt6::Quick QMapLibre::Core mov_ui_qmlplugin
)

qt_add_executable(metoceanviewer WIN32 MACOSX_BUNDLE main.cpp)
target_link_libraries(metoceanviewer PRIVATE mov::ui mov::options mov::warnings)
mov_enable_static_analysis(metoceanviewer)
mov_enforce_layer(metoceanviewer ui)
mov_stage_maplibre_runtime(metoceanviewer)
# Placeholders until the packaging step generates Info.plist (plan §4).
set_target_properties(
    metoceanviewer
    PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.zcobell.metoceanviewer
        MACOSX_BUNDLE_BUNDLE_NAME MetOceanViewer
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)
