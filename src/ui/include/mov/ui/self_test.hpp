// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <QStringList>
#include <cstdint>
#include <iosfwd>

class QQmlApplicationEngine;

/// The packaging smoke test (docs/packaging.md): `metoceanviewer --self-test`
/// checks that a packaged application finds what it loads at run time.
namespace mov::ui {

enum class SelfTestMode : std::uint8_t {
  none,    // no self-test: start the application
  basic,   // --self-test: no window, no GL, no network
  render,  // --self-test=render: basic, then one map frame drawn with GL
};

/// The self-test the command line asks for.
[[nodiscard]] SelfTestMode self_test_mode(const QStringList& arguments);

/// Windows: a GUI-subsystem program has no console, so when its standard
/// output is not redirected the self-test writes to the console of the
/// shell that started it. Elsewhere it does nothing.
void attach_parent_console();

/// The checks of --self-test, one line each to `out`:
///   - Qt Location loads the "maplibre" geoservices plugin (its path) and
///     its mapping engine;
///   - the main window's QML compiles (every QML import is deployed);
///   - the window icon renders (the SVG image plugin is deployed);
///   - Qt's TLS backend works (HTTPS tiles) and Qt Sql has SQLite (MapLibre's
///     tile cache);
///   - Windows: the active code page is UTF-8 (the manifest is embedded);
///   - PROJ opens the packaged proj.db (or the one MOV_PROJ_DATA names),
///     not some other copy, and converts EPSG:26915;
///   - the netCDF-C version.
/// No window, GL or network. Call it after the startup steps of startup.hpp.
/// Returns true when every check passed.
[[nodiscard]] bool run_self_test(std::ostream& out);

/// --self-test=render: loads the main window with a local background-only
/// style and, from the event loop, waits (at most 30 s) until the map area
/// shows its colour, then reports to `out` and ends the event loop with exit
/// code 0 or 1. Needs a display and GL. Returns false if the window did not
/// load (reported already).
[[nodiscard]] bool start_render_self_test(QQmlApplicationEngine& engine,
                                          std::ostream& out);

}  // namespace mov::ui
