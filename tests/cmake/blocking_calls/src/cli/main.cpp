// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// main() may run the event loop, and nothing else.
int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  QCoreApplication::processEvents();
  return app.exec();
}
