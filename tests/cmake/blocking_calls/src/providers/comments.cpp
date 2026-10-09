// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Comments may name what they must not call; run.cmake expects nothing here
// to be reported: processEvents(), waitForFinished(), loop.exec().

/// Never calls QEventLoop::exec() or future.result().
void comments() {
  int count = 0;  // not count.wait() either
  /* sleep_for(1s) */ ++count;
  const char* url = "https://example.org";  // QSemaphore
}
