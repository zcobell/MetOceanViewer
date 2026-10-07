// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// GUI tests (label gui): they render, so they need a display and OpenGL. On
// Linux they run under xvfb-run with Mesa (tests/CMakeLists.txt).

#include <QColor>
#include <QDeadlineTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickWindow>
#include <QString>
#include <QTest>
#include <QUrl>
#include <QVariantMap>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <utility>

#include "maplibre_provider.hpp"
#include "mov/test/fixture.hpp"
#include "mov/ui/startup.hpp"

using namespace std::chrono_literals;

namespace {

// The background colour of tests/fixtures/ui/background-style.json.
const QColor fixture_background{0x12, 0xab, 0x34};

// Loads the main window and fails the test on every QML warning, reported as
// it happens: to QML, a binding that fails at run time (an unresolved type,
// an undefined property) is only a warning.
QQuickWindow& load_main_window(QQmlApplicationEngine& engine) {
  QObject::connect(
      &engine, &QQmlEngine::warnings, [](const QList<QQmlError>& warnings) {
        for (const QQmlError& warning : warnings) {
          FAIL_CHECK("QML warning: " << warning.toString().toStdString());
        }
      });
  REQUIRE(mov::ui::load_main_window(engine));
  auto* const window =
      qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
  REQUIRE(window != nullptr);
  REQUIRE(QTest::qWaitForWindowExposed(window));
  return *window;
}

// Waits until the Qt Location Map (MapView.map) is ready: the plugin has
// created and sized its map. Independent of the network.
void wait_for_map(const QQuickWindow& window) {
  const auto* const map_view =
      window.findChild<QObject*>(QStringLiteral("mapView"));
  REQUIRE(map_view != nullptr);
  const auto* const map = map_view->property("map").value<QObject*>();
  REQUIRE(map != nullptr);
  REQUIRE(QTest::qWaitFor([map] { return map->property("mapReady").toBool(); },
                          QDeadlineTimer(30s)));
}

// A pixel of the map area: the top bar and the attribution sit at the edges.
QColor map_area_pixel(QQuickWindow& window) {
  const QImage image = window.grabWindow();
  return image.pixelColor(image.width() / 2, image.height() / 2);
}

// Waits until the window's frames have stopped changing for 1.5 s (the
// basemap's tiles, labels and fades are in), for at most `timeout`. Returns
// whether they settled.
bool wait_for_settled_frames(QQuickWindow& window,
                             const std::chrono::milliseconds timeout) {
  constexpr auto quiet = 1500ms;
  QImage last;
  QElapsedTimer unchanged;
  unchanged.start();
  return QTest::qWaitFor(
      [&] {
        QImage frame = window.grabWindow();
        if (frame != last) {
          last = std::move(frame);
          unchanged.restart();
          return false;
        }
        return unchanged.durationElapsed() >= quiet;
      },
      QDeadlineTimer(timeout));
}

// Saves the window's current frame as MOV_SCREENSHOT_DIR/<name> and returns
// the path.
QString save_screenshot(QQuickWindow& window, const char* name) {
  const QImage frame = window.grabWindow();
  REQUIRE_FALSE(frame.isNull());
  const QDir dir(QStringLiteral(MOV_SCREENSHOT_DIR));
  REQUIRE(dir.mkpath(QStringLiteral(".")));
  const QString path = dir.filePath(QString::fromUtf8(name));
  REQUIRE(frame.save(path));
  return path;
}

bool same_colour(const QColor& a, const QColor& b) {
  constexpr int tolerance = 2;
  return std::abs(a.red() - b.red()) <= tolerance and
         std::abs(a.green() - b.green()) <= tolerance and
         std::abs(a.blue() - b.blue()) <= tolerance;
}

}  // namespace

// The walking skeleton's smoke test, with no network: MapLibre draws a local
// background-only style in a real GL context, inside the QML scene.
TEST_CASE("The main window renders its map with MapLibre") {
  mov::test::require_maplibre_provider();

  QQmlApplicationEngine engine;
  const QString style =
      QUrl::fromLocalFile(
          QString::fromStdString(
              mov::test::fixture("ui/background-style.json").generic_string()))
          .toString();
  engine.setInitialProperties({{QStringLiteral("basemapStyle"), style}});
  QQuickWindow& window = load_main_window(engine);
  wait_for_map(window);

  // Sample from mapReady on. With this static style MapLibre draws the
  // colour on its first frames, but the re-render the Location plugin
  // schedules 250 ms after the map is fully loaded leaves the map area blank
  // (Map's #e6e6e6 background); the OpenFreeMap basemap does not do this.
  // Seen with MapLibre Native Qt c3485f3a; to investigate (and report
  // upstream) with the Phase 4 map work. Polling catches the drawn frames.
  const bool rendered = QTest::qWaitFor(
      [&window] {
        return same_colour(map_area_pixel(window), fixture_background);
      },
      QDeadlineTimer(30s));
  INFO("map-area pixel: " << map_area_pixel(window).name().toStdString()
                          << ", expected "
                          << fixture_background.name().toStdString());
  if (not rendered) {
    const QString path = save_screenshot(window, "render-check-failure.png");
    UNSCOPED_INFO("frame saved to " << path.toStdString());
  }
  CHECK(rendered);
}

// For a human to look at, not a gate on the basemap: with the default
// (OpenFreeMap) style it waits up to 20 s for the frames to settle, then
// saves build/<preset>/tests/ui/screenshots/main-window.png. Offline it saves
// an empty map and still passes.
TEST_CASE("Screenshot of the main window with the default basemap",
          "[screenshot]") {
  QQmlApplicationEngine engine;
  QQuickWindow& window = load_main_window(engine);
  wait_for_map(window);
  if (not wait_for_settled_frames(window, 20s)) {
    WARN("The map was still changing after 20 s");
  }

  WARN("Screenshot: "
       << save_screenshot(window, "main-window.png").toStdString());
}
