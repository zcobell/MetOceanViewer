# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

mov_add_test(mov_ui_tests GUI SOURCES main.cpp test_main_window.cpp LIBRARIES mov::ui mov::core Qt6::Quick Qt6::Test)
mov_stage_maplibre_runtime(mov_ui_tests)
target_compile_definitions(mov_ui_tests PRIVATE "MOV_SCREENSHOT_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/screenshots\"")
