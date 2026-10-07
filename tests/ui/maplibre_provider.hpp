// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <QGeoServiceProvider>
#include <QString>
#include <QStringList>
#include <catch2/catch_test_macros.hpp>

namespace mov::test {

/// Requires that Qt Location finds the "maplibre" geoservices plugin and that
/// it creates its mapping engine. Needs no GL context, so it pins down
/// plugin staging and DLL/RUNPATH problems apart from rendering ones.
inline void require_maplibre_provider() {
  const QGeoServiceProvider provider(QStringLiteral("maplibre"));
  INFO("available providers: "
       << QGeoServiceProvider::availableServiceProviders()
              .join(QStringLiteral(", "))
              .toStdString());
  INFO("error: " << provider.errorString().toStdString());
  REQUIRE(provider.error() == QGeoServiceProvider::NoError);
  REQUIRE(provider.mappingManager() != nullptr);
  INFO("mapping error: " << provider.mappingErrorString().toStdString());
  REQUIRE(provider.mappingError() == QGeoServiceProvider::NoError);
}

}  // namespace mov::test
