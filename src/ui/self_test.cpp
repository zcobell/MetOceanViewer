// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/ui/self_test.hpp"

#include <QGeoServiceProvider>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <array>
#include <expected>
#include <functional>
#include <ostream>
#include <string>
#include <string_view>

#include "mov/core/geo.hpp"
#include "mov/io/netcdf_library.hpp"
#include "mov/io/projection.hpp"

namespace mov::ui {

namespace {

// A check's outcome: a detail to print on success, the reason on failure.
using Outcome = std::expected<std::string, std::string>;

Outcome check_maplibre_plugin() {
  const QGeoServiceProvider provider(QStringLiteral("maplibre"));
  if (provider.error() != QGeoServiceProvider::NoError) {
    return std::unexpected{provider.errorString().toStdString() +
                           " (available: " +
                           QGeoServiceProvider::availableServiceProviders()
                               .join(QStringLiteral(", "))
                               .toStdString() +
                           ")"};
  }
  if (provider.mappingManager() == nullptr or
      provider.mappingError() != QGeoServiceProvider::NoError) {
    return std::unexpected{provider.mappingErrorString().toStdString()};
  }
  return "loaded";
}

// Compiles the main window without creating it: resolves every import of
// the module's QML, but needs neither a window nor GL.
Outcome check_main_qml() {
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.loadFromModule("MetOceanViewer", "Main");
  if (not component.isReady()) {
    return std::unexpected{component.errorString().trimmed().toStdString()};
  }
  return "compiled";
}

Outcome check_window_icon() {
  constexpr int size = 64;
  if (QGuiApplication::windowIcon().pixmap(size).isNull()) {
    return std::unexpected{std::string{"empty (no SVG image plugin?)"}};
  }
  return "renders";
}

// EPSG:4326 never opens the database; NAD83 / UTM 15N does.
Outcome check_projection_database() {
  constexpr int utm_15n_nad83 = 26915;
  const auto crs = core::Epsg::make(utm_15n_nad83);
  if (not crs) {
    return std::unexpected{std::string{"bad EPSG code"}};
  }
  if (const auto projector = io::Projector::make(*crs); not projector) {
    return std::unexpected{
        projector.error().code == io::ProjectionErrc::database_unavailable
            ? std::string{"proj.db not found (set MOV_PROJ_DATA?)"}
            : std::string{"EPSG:26915 failed"}};
  }
  return "EPSG:26915 to WGS84";
}

Outcome check_netcdf() {
  const std::string_view version = io::netcdf_library_version();
  if (version.empty()) {
    return std::unexpected{std::string{"no version"}};
  }
  return std::string{version};
}

bool report(std::ostream& out, std::string_view name, const Outcome& outcome) {
  out << (outcome ? "ok   " : "FAIL ") << name << ": "
      << (outcome ? *outcome : outcome.error()) << '\n';
  return outcome.has_value();
}

}  // namespace

bool self_test_requested(const QStringList& arguments) {
  return arguments.contains(QStringLiteral("--self-test"));
}

bool run_self_test(std::ostream& out) {
  // Every check runs (in order: a braced list is evaluated left to right),
  // so one run reports every missing piece.
  const std::array results{
      report(out, "maplibre geoservices plugin", check_maplibre_plugin()),
      report(out, "QML module MetOceanViewer", check_main_qml()),
      report(out, "window icon", check_window_icon()),
      report(out, "PROJ database", check_projection_database()),
      report(out, "netCDF-C", check_netcdf()),
  };
  const bool passed = std::ranges::all_of(results, std::identity{});
  out << (passed ? "self-test passed\n" : "self-test FAILED\n") << std::flush;
  return passed;
}

}  // namespace mov::ui
