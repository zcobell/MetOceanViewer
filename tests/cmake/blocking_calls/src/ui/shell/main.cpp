// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Only src/<layer>/main.cpp may run the event loop.
int nested_main() { return QGuiApplication::exec(); }
