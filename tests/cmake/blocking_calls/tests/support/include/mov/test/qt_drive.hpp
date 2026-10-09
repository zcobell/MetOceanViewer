// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The test driver may block: nothing here is reported.
#pragma once

inline void drive() {
  QEventLoop loop;
  loop.processEvents();
  static_cast<void>(loop.exec());
  QTest::qWait(1);
}
