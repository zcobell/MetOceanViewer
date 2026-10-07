// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QImage>
#include <QList>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickWindow>
#include <QString>
#include <QTest>
#include <catch2/catch_test_macros.hpp>
#include <chrono>

#include "mov/core/version.hpp"
#include "mov/ui/startup.hpp"

using namespace std::chrono_literals;

TEST_CASE("The application reports the project version") {
  CHECK(QCoreApplication::applicationVersion() ==
        QString::fromUtf8(mov::core::version()));
}

// The walking skeleton's smoke test: the QML scene loads without warnings,
// the "maplibre" plugin creates its map in a real GL context, and the window
// renders. It passes offline too: the style and tiles load asynchronously, so
// without a network the map is merely empty. The screenshot is for a human to
// look at.
TEST_CASE("The main window shows a MapLibre map") {
  QQmlApplicationEngine engine;
  // A binding that fails at run time (an unresolved type, an undefined
  // property) is only a warning to QML; here it fails the test.
  QList<QQmlError> qml_warnings;
  QObject::connect(&engine, &QQmlEngine::warnings,
                   [&qml_warnings](const QList<QQmlError>& warnings) {
                     qml_warnings.append(warnings);
                   });
  REQUIRE(mov::ui::load_main_window(engine));
  auto* const window =
      qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
  REQUIRE(window != nullptr);
  REQUIRE(QTest::qWaitForWindowExposed(window));

  // MapView.map is the Qt Location Map; mapReady turns true once the plugin
  // has created and sized its map.
  const auto* const map_view =
      window->findChild<QObject*>(QStringLiteral("mapView"));
  REQUIRE(map_view != nullptr);
  const auto* const map = map_view->property("map").value<QObject*>();
  REQUIRE(map != nullptr);
  REQUIRE(QTest::qWaitFor([map] { return map->property("mapReady").toBool(); },
                          QDeadlineTimer(30s)));

  // Online, give the style and the first tiles time to arrive.
  QTest::qWait(5s);
  const QImage screenshot = window->grabWindow();
  REQUIRE_FALSE(screenshot.isNull());
  const QDir dir(QStringLiteral(MOV_SCREENSHOT_DIR));
  REQUIRE(dir.mkpath(QStringLiteral(".")));
  const QString path = dir.filePath(QStringLiteral("main-window.png"));
  REQUIRE(screenshot.save(path));
  WARN("Screenshot: " << path.toStdString());

  for (const QQmlError& warning : qml_warnings) {
    UNSCOPED_INFO(warning.toString().toStdString());
  }
  CHECK(qml_warnings.isEmpty());
}
