// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The marker counts only in atomic_file.cpp.
void sibling() { ::Sleep(sibling_ms); }  // gate: bounded-retry
