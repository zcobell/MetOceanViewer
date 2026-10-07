// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <cstdlib>
#include <iostream>

#include "mov/ui/self_test.hpp"
#include "mov/ui/startup.hpp"

int main(int argc, char* argv[]) {
  mov::ui::select_graphics_api();
  const QGuiApplication app(argc, argv);
  mov::ui::set_application_metadata();
  mov::ui::configure_projection_data();

  // The packaging smoke test (docs/packaging.md): no window, exit status.
  if (mov::ui::self_test_requested(QGuiApplication::arguments())) {
    return mov::ui::run_self_test(std::cout) ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  QQmlApplicationEngine engine;
  if (not mov::ui::load_main_window(engine)) {
    return EXIT_FAILURE;
  }
  return QGuiApplication::exec();
}
