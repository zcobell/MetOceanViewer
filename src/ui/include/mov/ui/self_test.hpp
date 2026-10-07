// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <QStringList>
#include <iosfwd>

namespace mov::ui {

/// True when the command line asks for the self-test (`--self-test`).
[[nodiscard]] bool self_test_requested(const QStringList& arguments);

/// `metoceanviewer --self-test`: checks, without showing a window or touching
/// the network, that a packaged application can find what it loads at run
/// time, and writes one line per check to `out`:
///   - Qt Location loads the "maplibre" geoservices plugin and its mapping
///     engine (the plugin and the QMapLibre libraries are deployed);
///   - the main window's QML compiles (every QML import is deployed);
///   - the window icon renders (the SVG image plugin is deployed);
///   - PROJ opens its database (proj.db is shipped and found);
///   - the netCDF-C version.
/// Needs no GL, so it runs on the offscreen platform. Call it after the
/// startup steps of startup.hpp. Returns true when every check passed.
[[nodiscard]] bool run_self_test(std::ostream& out);

}  // namespace mov::ui
