// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <cstdlib>

#include "mov/ui/startup.hpp"

int main(int argc, char* argv[]) {
  mov::ui::select_graphics_api();
  const QGuiApplication app(argc, argv);
  mov::ui::set_application_metadata();

  QQmlApplicationEngine engine;
  if (not mov::ui::load_main_window(engine)) {
    return EXIT_FAILURE;
  }
  return QGuiApplication::exec();
}
