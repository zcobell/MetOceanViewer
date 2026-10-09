# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# Label qt: the providers and the test driver, offscreen, no network.
mov_add_test(
    mov_providers_tests
    QT
    SOURCES main.cpp test_user_agent.cpp test_qt_drive.cpp
    LIBRARIES mov::providers Qt6::Core
)
