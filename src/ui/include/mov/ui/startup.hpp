// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <filesystem>
#include <optional>

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
/// app_identity, plan §6.19, which also namespaces QSettings), the version
/// from project(VERSION) and the window icon. Must run after the
/// QGuiApplication exists.
void set_application_metadata();

/// The directory a package ships proj.db in: app_identity::proj_data_dir
/// relative to the executable (share/metoceanviewer/proj beside bin/, or
/// Resources/proj in a macOS bundle; the build tree has the same layout).
/// Must run after the QGuiApplication exists.
[[nodiscard]] std::filesystem::path packaged_projection_data_dir();

/// Tells mov::io to open the packaged proj.db when that directory has one,
/// and returns the directory; otherwise PROJ keeps its own defaults and the
/// result is nullopt. `MOV_PROJ_DATA` in the environment still overrides it
/// (mov::io::set_projection_data_dir). Must run after the QGuiApplication
/// exists and before the first projection. Moves to src/app with the
/// application state in Phase 4.
std::optional<std::filesystem::path> configure_projection_data();

/// Loads the main window (app_identity::qml_module, main_window_type) into
/// `engine`. Returns false if it failed to load; the QML errors are already
/// logged.
[[nodiscard]] bool load_main_window(QQmlApplicationEngine& engine);

}  // namespace mov::ui
