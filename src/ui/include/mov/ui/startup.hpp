// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

class QQmlApplicationEngine;

/// Startup steps shared by the application's main() and the GUI tests, in
/// call order. None of them touches the network: the map fetches its style and
/// tiles asynchronously, and offline is a normal state.
namespace mov::ui {

/// Selects the graphics API MapLibre renders with: OpenGL on Linux and
/// Windows, Metal on macOS. Must run before the QGuiApplication exists.
void select_graphics_api();

/// Sets the application name, version (from project(VERSION)) and window
/// icon. Must run after the QGuiApplication exists.
void set_application_metadata();

/// Loads the main window (MetOceanViewer/Main.qml) into `engine`. Returns
/// false if it failed to load; the QML errors are already logged.
[[nodiscard]] bool load_main_window(QQmlApplicationEngine& engine);

}  // namespace mov::ui
