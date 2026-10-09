// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Catch2 main of the providers' Qt tests: one QCoreApplication per process,
// created before any test case. It runs no event loop; a test that needs
// one runs it through mov::test::drive_until.

#include <QCoreApplication>
#include <catch2/catch_session.hpp>

int main(int argc, char* argv[]) {
  const QCoreApplication app(argc, argv);
  return Catch::Session().run(argc, argv);
}
