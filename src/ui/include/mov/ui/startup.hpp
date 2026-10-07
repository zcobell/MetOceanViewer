// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

class QQmlApplicationEngine;

/// Startup steps shared by the application's main() and the Qt tests, in
/// call order. None of them touches the network: the map fetches its style and
/// tiles asynchronously, and offline is a normal state.
namespace mov::ui {

/// Makes Qt Quick render with the graphics API MapLibre Native Qt was built
/// for (chosen per platform in cmake/vcpkg-ports/maplibre-native-qt/
/// portfile.cmake). Must run before the QGuiApplication exists.
void select_graphics_api();

/// Sets the application identity (names, organization, desktop file name:
/// the io.github.zcobell.metoceanviewer identity of plan §6.19, which also
/// namespaces QSettings), the version from project(VERSION) and the window
/// icon. Must run after the QGuiApplication exists.
void set_application_metadata();

/// Loads the main window (MetOceanViewer/Main.qml) into `engine`. Returns
/// false if it failed to load; the QML errors are already logged.
[[nodiscard]] bool load_main_window(QQmlApplicationEngine& engine);

}  // namespace mov::ui
