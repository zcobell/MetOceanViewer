// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Catch2 main of the Qt tests (labels qt and gui). Qt needs one QGuiApplication
// per process, created before any test case, with the graphics API chosen
// first: the same startup steps as the application's main().

#include <QGuiApplication>
#include <catch2/catch_session.hpp>

#include "mov/ui/startup.hpp"

int main(int argc, char* argv[]) {
  mov::ui::select_graphics_api();
  const QGuiApplication app(argc, argv);
  mov::ui::set_application_metadata();
  return Catch::Session().run(argc, argv);
}
