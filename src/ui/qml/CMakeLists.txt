# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# QML module "MetOceanViewer". qmlcachegen compiles the files at build time;
# `cmake --build <dir> --target all_qmllint` type-checks them (CI runs it).
# Qt's CMake offers no way to pass qmllint arguments, so .qmllint.ini here
# makes any warning fail the lint. Its C++ is all generated, so mov::warnings
# is not applied.

# Before qt_add_qml_module, which writes the qmldir.
set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(mov_ui_qml STATIC)
qt_add_qml_module(
    mov_ui_qml
    URI MetOceanViewer
    VERSION 1.0
    # The directory must end in the module's URI for qmllint to resolve it.
    OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/qml/MetOceanViewer"
    QML_FILES
        AboutDialog.qml
        Main.qml
        Theme.qml
        TopBar.qml
    RESOURCES images/app-icon.svg
)
# One Qt link per QML import (QtQuick, QtQuick.Controls, QtLocation,
# QtPositioning), so qmlcachegen and qmllint see their type information.
target_link_libraries(mov_ui_qml PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Location Qt6::Positioning)
mov_enforce_layer(mov_ui_qml ui)
mov_enforce_layer(mov_ui_qmlplugin ui)
