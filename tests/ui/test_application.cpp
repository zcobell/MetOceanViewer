// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Qt tests that need no display or GL (label qt, QT_QPA_PLATFORM=offscreen),
// so they run on every CI platform.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QtEnvironmentVariables>
#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>

#include "maplibre_provider.hpp"
#include "mov/core/version.hpp"
#include "mov/ui/app_identity.hpp"
#include "mov/ui/self_test.hpp"
#include "mov/ui/startup.hpp"

TEST_CASE("The application reports its identity and the project version") {
  CHECK(QCoreApplication::applicationName() ==
        QStringLiteral("MetOceanViewer"));
  CHECK(QCoreApplication::applicationName().toStdString() ==
        mov::ui::app_identity::name);
  CHECK(QCoreApplication::applicationVersion() ==
        QString::fromUtf8(mov::core::version()));
  // QSettings and the platform identity derive from these (plan §6.19).
  CHECK(QCoreApplication::organizationName() ==
        QStringLiteral("MetOceanViewer"));
  CHECK(QCoreApplication::organizationDomain() ==
        QStringLiteral("zcobell.github.io"));
  // The Linux .desktop entry is <app id>.desktop (cmake/Packaging.cmake).
  CHECK(QGuiApplication::desktopFileName() ==
        QStringLiteral("io.github.zcobell.metoceanviewer"));
  CHECK(QGuiApplication::desktopFileName().toStdString() ==
        mov::ui::app_identity::id);
  // A wrong resource path gives a null icon, not an error.
  CHECK_FALSE(QGuiApplication::windowIcon().pixmap(64).isNull());
}

TEST_CASE("Qt Location loads the maplibre geoservices plugin") {
  mov::test::require_maplibre_provider();
}

TEST_CASE("--self-test and --self-test=render are recognised") {
  using mov::ui::SelfTestMode;
  const QString app = QStringLiteral("metoceanviewer");
  CHECK(mov::ui::self_test_mode({app, QStringLiteral("--self-test")}) ==
        SelfTestMode::basic);
  CHECK(mov::ui::self_test_mode({app, QStringLiteral("--self-test=render")}) ==
        SelfTestMode::render);
  CHECK(mov::ui::self_test_mode({app}) == SelfTestMode::none);
  CHECK(mov::ui::self_test_mode({app, QStringLiteral("session.mvs")}) ==
        SelfTestMode::none);
}

// The build stages what a package ships (the maplibre plugin, proj.db), so
// the self-test passes here as it must in a package.
TEST_CASE("The self-test passes in the build tree") {
  std::ostringstream out;
  const bool passed = mov::ui::run_self_test(out);
  INFO(out.str());
  CHECK(passed);
  CHECK(out.str().find("self-test passed") != std::string::npos);
  // It names the database it checked: the staged copy.
  const std::string staged =
      (mov::ui::packaged_projection_data_dir() / "proj.db").string();
  CHECK(out.str().find(staged) != std::string::npos);
}

// The PROJ check can fail: a directory without proj.db is no database, and
// the report says where it looked.
TEST_CASE("The self-test fails without the PROJ database") {
  const QTemporaryDir empty;
  REQUIRE(empty.isValid());
  REQUIRE(qputenv("MOV_PROJ_DATA", empty.path().toUtf8()));
  std::ostringstream out;
  const bool passed = mov::ui::run_self_test(out);
  qunsetenv("MOV_PROJ_DATA");
  INFO(out.str());
  CHECK_FALSE(passed);
  CHECK(out.str().find("FAIL PROJ database: no usable proj.db; looked in") !=
        std::string::npos);
  CHECK(out.str().find("(from MOV_PROJ_DATA)") != std::string::npos);
}
