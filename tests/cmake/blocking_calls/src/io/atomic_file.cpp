// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The marked pause is allowed here, an unmarked one is not.
void rename_with_retries() {
  ::Sleep(pause_ms);  // gate: bounded-retry
  ::Sleep(unmarked_ms);
}
