// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// What the line scanner must get right; run.cmake names each case.

/// Never calls processEvents, polls in [t0, t1)
void unbalanced() { QCoreApplication::processEvents(); }

// §2.3: never waitForFinished()
int section = 0;  // §2.3: never waitForFinished()

void comment_in_string() { call("/*", dialog.exec()); }  // */

void quote_literal() {
  const char quote = '"';  // never processEvents()
}
