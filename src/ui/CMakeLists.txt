# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# The Qt Quick shell:
#   mov_ui          startup code shared by the app and the GUI tests
#   mov_ui_qml      QML module "MetOceanViewer" (qml/)
#   metoceanviewer  the application

qt_standard_project_setup(REQUIRES 6.11)

add_subdirectory(qml)

mov_add_module(
    ui
    SOURCES startup.cpp
    PUBLIC_LINK Qt6::Gui Qt6::Qml
    PRIVATE_LINK mov::core Qt6::Quick QMapLibre::Location mov_ui_qmlplugin
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
