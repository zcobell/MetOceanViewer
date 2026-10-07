// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Qt tests that need no display or GL (label qt, QT_QPA_PLATFORM=offscreen),
// so they run on every CI platform.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QString>
#include <QStringList>
#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>

#include "maplibre_provider.hpp"
#include "mov/core/version.hpp"
#include "mov/ui/self_test.hpp"

TEST_CASE("The application reports its identity and the project version") {
  CHECK(QCoreApplication::applicationName() ==
        QStringLiteral("MetOceanViewer"));
  CHECK(QCoreApplication::applicationVersion() ==
        QString::fromUtf8(mov::core::version()));
  // QSettings and the platform identity derive from these (plan §6.19).
  CHECK(QCoreApplication::organizationName() ==
        QStringLiteral("MetOceanViewer"));
  CHECK(QCoreApplication::organizationDomain() ==
        QStringLiteral("zcobell.github.io"));
  CHECK(QGuiApplication::desktopFileName() ==
        QStringLiteral("io.github.zcobell.metoceanviewer"));
  // A wrong resource path gives a null icon, not an error.
  CHECK_FALSE(QGuiApplication::windowIcon().pixmap(64).isNull());
}

TEST_CASE("Qt Location loads the maplibre geoservices plugin") {
  mov::test::require_maplibre_provider();
}

TEST_CASE("--self-test is recognised among the arguments") {
  CHECK(mov::ui::self_test_requested(
      {QStringLiteral("metoceanviewer"), QStringLiteral("--self-test")}));
  CHECK_FALSE(mov::ui::self_test_requested({QStringLiteral("metoceanviewer")}));
  CHECK_FALSE(mov::ui::self_test_requested(
      {QStringLiteral("metoceanviewer"), QStringLiteral("session.mvs")}));
}

// The build stages what a package ships (the maplibre plugin, proj.db), so
// the self-test passes here as it must in a package.
TEST_CASE("The self-test passes in the build tree") {
  std::ostringstream out;
  const bool passed = mov::ui::run_self_test(out);
  INFO(out.str());
  CHECK(passed);
  CHECK(out.str().find("self-test passed") != std::string::npos);
}
