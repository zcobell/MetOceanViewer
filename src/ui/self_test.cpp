// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/ui/self_test.hpp"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGeoServiceProvider>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSqlDatabase>
#include <QSslSocket>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "mov/core/geo.hpp"
#include "mov/io/netcdf_library.hpp"
#include "mov/io/projection.hpp"
#include "mov/ui/app_identity.hpp"
#include "mov/ui/startup.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace mov::ui {

namespace {

using namespace std::chrono_literals;

// A check's outcome: a detail to print on success, the reason on failure.
using Outcome = std::expected<std::string, std::string>;

[[nodiscard]] QString to_qstring(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// The maplibre plugin file Qt's plugin search would find, for the report.
[[nodiscard]] std::optional<QString> maplibre_plugin_file() {
  for (const QString& dir : QCoreApplication::libraryPaths()) {
    const QFileInfoList found =
        QDir(dir + QStringLiteral("/geoservices"))
            .entryInfoList({QStringLiteral("*maplibre*")}, QDir::Files);
    if (not found.isEmpty()) {
      return found.constFirst().absoluteFilePath();
    }
  }
  return std::nullopt;
}

[[nodiscard]] Outcome check_maplibre_plugin() {
  const QGeoServiceProvider provider(QStringLiteral("maplibre"));
  if (provider.error() != QGeoServiceProvider::NoError) {
    return std::unexpected{provider.errorString().toStdString() +
                           " (available: " +
                           QGeoServiceProvider::availableServiceProviders()
                               .join(QStringLiteral(", "))
                               .toStdString() +
                           "; plugin search path: " +
                           QCoreApplication::libraryPaths()
                               .join(QStringLiteral(", "))
                               .toStdString() +
                           ")"};
  }
  if (provider.mappingManager() == nullptr or
      provider.mappingError() != QGeoServiceProvider::NoError) {
    return std::unexpected{"no mapping engine: " +
                           provider.mappingErrorString().toStdString()};
  }
  return "loaded from " +
         maplibre_plugin_file().value_or(QStringLiteral("?")).toStdString();
}

// Compiles the main window without creating it: resolves every import of
// the module's QML, but needs neither a window nor GL.
[[nodiscard]] Outcome check_main_qml() {
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.loadFromModule(to_qstring(app_identity::qml_module),
                           to_qstring(app_identity::main_window_type));
  if (not component.isReady()) {
    return std::unexpected{component.errorString().trimmed().toStdString()};
  }
  return "compiled";
}

[[nodiscard]] Outcome check_window_icon() {
  constexpr int size = 64;
  if (QGuiApplication::windowIcon().pixmap(size).isNull()) {
    return std::unexpected{std::string{"empty (no SVG image plugin?)"}};
  }
  return "renders";
}

// The basemap and its tiles are HTTPS.
[[nodiscard]] Outcome check_tls() {
  if (not QSslSocket::supportsSsl()) {
    return std::unexpected{"no TLS backend (available: " +
                           QSslSocket::availableBackends()
                               .join(QStringLiteral(", "))
                               .toStdString() +
                           ")"};
  }
  return "backend " + QSslSocket::activeBackend().toStdString();
}

// MapLibre's tile cache is an SQLite database through Qt Sql.
[[nodiscard]] Outcome check_sqlite() {
  if (not QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
    return std::unexpected{
        "no QSQLITE driver (available: " +
        QSqlDatabase::drivers().join(QStringLiteral(", ")).toStdString() + ")"};
  }
  return "QSQLITE";
}

#if defined(_WIN32)
// netCDF-C reads paths in the active code page; the manifest makes it UTF-8.
[[nodiscard]] Outcome check_code_page() {
  const UINT code_page = GetACP();
  if (code_page != CP_UTF8) {
    return std::unexpected{"active code page " + std::to_string(code_page) +
                           ", not UTF-8 (is the manifest embedded?)"};
  }
  return "UTF-8";
}
#endif

[[nodiscard]] std::string reason(io::ProjectionErrc code) {
  switch (code) {
    case io::ProjectionErrc::unknown_crs:
      return "EPSG:26915 is not in the database";
    case io::ProjectionErrc::transform_failed:
      return "no transformation from EPSG:26915 to WGS84";
    case io::ProjectionErrc::database_unavailable:
      return "the database is unavailable";
  }
  std::unreachable();
}

// The database PROJ opens must be the one shipped beside the executable (or
// the one MOV_PROJ_DATA names): any other copy, such as one built in, would
// hide a package that lacks its database.
[[nodiscard]] Outcome check_projection_database() {
  const QString override_dir = qEnvironmentVariable("MOV_PROJ_DATA");
  const std::filesystem::path expected_dir =
      override_dir.isEmpty()
          ? packaged_projection_data_dir()
          : std::filesystem::path{override_dir.toStdU16String()};
  const std::string source =
      override_dir.isEmpty() ? std::string{} : " (from MOV_PROJ_DATA)";
  const std::optional<std::filesystem::path> used =
      io::projection_database_path();
  if (not used) {
    return std::unexpected{"no usable proj.db; looked in " +
                           expected_dir.string() + source};
  }
  std::error_code error;
  if (not std::filesystem::equivalent(used->parent_path(), expected_dir,
                                      error)) {
    return std::unexpected{used->string() + " is not the one in " +
                           expected_dir.string() + source};
  }
  constexpr auto utm_15n_nad83 = core::Epsg::make(26915);
  static_assert(utm_15n_nad83.has_value());
  if (const auto projector = io::Projector::make(*utm_15n_nad83);
      not projector) {
    return std::unexpected{used->string() + ": " +
                           reason(projector.error().code)};
  }
  return used->string() + source;
}

// "4.9.3 of Oct  7 2026 ..." -> "4.9.3".
[[nodiscard]] Outcome check_netcdf() {
  const std::string_view text = io::netcdf_library_version();
  const std::string_view version = text.substr(0, text.find(' '));
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

// The colour of the render check's style, and how close a pixel must be.
constexpr std::string_view render_style_json = R"({
  "version": 8,
  "name": "MetOceanViewer self-test: one opaque colour, no sources",
  "sources": {},
  "layers": [{"id": "background", "type": "background",
              "paint": {"background-color": "#12ab34"}}]
})";
const QColor render_colour{0x12, 0xab, 0x34};

[[nodiscard]] bool same_colour(const QColor& a, const QColor& b) {
  constexpr int tolerance = 2;
  return std::abs(a.red() - b.red()) <= tolerance and
         std::abs(a.green() - b.green()) <= tolerance and
         std::abs(a.blue() - b.blue()) <= tolerance;
}

}  // namespace

SelfTestMode self_test_mode(const QStringList& arguments) {
  if (arguments.contains(QStringLiteral("--self-test=render"))) {
    return SelfTestMode::render;
  }
  if (arguments.contains(QStringLiteral("--self-test"))) {
    return SelfTestMode::basic;
  }
  return SelfTestMode::none;
}

void attach_parent_console() {
#if defined(_WIN32)
  // Only when nothing was redirected: a redirection's handles are inherited
  // and must stay.
  const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
  if ((handle == nullptr or handle == INVALID_HANDLE_VALUE) and
      AttachConsole(ATTACH_PARENT_PROCESS) != 0) {
    FILE* stream = nullptr;
    static_cast<void>(freopen_s(&stream, "CONOUT$", "w", stdout));
    static_cast<void>(freopen_s(&stream, "CONOUT$", "w", stderr));
    std::cout.clear();
    std::cerr.clear();
  }
#endif
}

bool run_self_test(std::ostream& out) {
  // Every check runs (in order: a braced list is evaluated left to right),
  // so one run reports every missing piece.
  const std::array results{
      report(out, "maplibre geoservices plugin", check_maplibre_plugin()),
      report(out, "QML module", check_main_qml()),
      report(out, "window icon", check_window_icon()),
      report(out, "TLS", check_tls()),
      report(out, "SQL driver", check_sqlite()),
#if defined(_WIN32)
      report(out, "code page", check_code_page()),
#endif
      report(out, "PROJ database", check_projection_database()),
      report(out, "netCDF-C", check_netcdf()),
  };
  const bool passed = std::ranges::all_of(results, std::identity{});
  out << (passed ? "self-test passed\n" : "self-test FAILED\n") << std::flush;
  return passed;
}

bool start_render_self_test(QQmlApplicationEngine& engine, std::ostream& out) {
  // The style file lives as long as the check (the lambda below holds it).
  auto style_dir = std::make_shared<QTemporaryDir>();
  const QString style_path = style_dir->filePath(QStringLiteral("style.json"));
  QFile style(style_path);
  if (not style_dir->isValid() or not style.open(QIODevice::WriteOnly) or
      style.write(render_style_json.data(),
                  static_cast<qint64>(render_style_json.size())) < 0) {
    out << "FAIL map renders: cannot write a style to "
        << style_path.toStdString() << '\n'
        << std::flush;
    return false;
  }
  style.close();
  engine.setInitialProperties({{QStringLiteral("basemapStyle"),
                                QUrl::fromLocalFile(style_path).toString()}});
  if (not load_main_window(engine)) {
    out << "FAIL map renders: the main window did not load\n" << std::flush;
    return false;
  }
  const QPointer<QQuickWindow> window =
      qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());

  // Polled from the event loop (no nested loop): MapLibre draws the colour
  // on its first frames after the map is ready, and a later re-render may
  // blank it again (tests/ui/test_main_window.cpp), so any frame showing it
  // passes.
  QElapsedTimer elapsed;
  elapsed.start();
  auto* const poll = new QTimer(&engine);  // owned by the engine
  QObject::connect(
      poll, &QTimer::timeout, poll, [poll, window, style_dir, elapsed, &out] {
        constexpr auto deadline = 30s;
        QColor seen;
        if (window) {
          const QImage frame = window->grabWindow();
          if (not frame.isNull()) {
            seen = frame.pixelColor(frame.width() / 2, frame.height() / 2);
          }
        }
        if (seen.isValid() and same_colour(seen, render_colour)) {
          poll->stop();
          out << "ok   map renders: " << render_colour.name().toStdString()
              << " at the map's centre\nself-test passed\n"
              << std::flush;
          QCoreApplication::exit(EXIT_SUCCESS);
        } else if (elapsed.durationElapsed() > deadline) {
          poll->stop();
          out << "FAIL map renders: no frame showed "
              << render_colour.name().toStdString() << " within 30 s (last "
              << (seen.isValid() ? seen.name().toStdString()
                                 : std::string{"none"})
              << ")\nself-test FAILED\n"
              << std::flush;
          QCoreApplication::exit(EXIT_FAILURE);
        }
      });
  constexpr auto interval = 50ms;
  poll->start(interval);
  return true;
}

}  // namespace mov::ui
