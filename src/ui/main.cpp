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
  static_cast<void>(mov::ui::configure_projection_data());

  // The packaging smoke test (docs/packaging.md): reports on stdout and
  // exits with its verdict.
  if (const auto mode = mov::ui::self_test_mode(QGuiApplication::arguments());
      mode != mov::ui::SelfTestMode::none) {
    mov::ui::attach_parent_console();
    if (not mov::ui::run_self_test(std::cout)) {
      return EXIT_FAILURE;
    }
    if (mode == mov::ui::SelfTestMode::basic) {
      return EXIT_SUCCESS;
    }
    QQmlApplicationEngine engine;
    if (not mov::ui::start_render_self_test(engine, std::cout)) {
      return EXIT_FAILURE;
    }
    return QGuiApplication::exec();
  }

  QQmlApplicationEngine engine;
  if (not mov::ui::load_main_window(engine)) {
    return EXIT_FAILURE;
  }
  return QGuiApplication::exec();
}
