# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# Label live (docs/providers-design.md §10.5): excluded by every test preset;
# run with `MOV_LIVE_API=1 ctest --test-dir build/<preset> -L live`.
mov_add_test(mov_live_tests LIVE SOURCES test_live_opt_in.cpp)

# The label's guard: red when the label runs without the opt-in.
add_test(NAME live_api_opt_in_guard COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/check_opt_in.cmake")
set_tests_properties(live_api_opt_in_guard PROPERTIES LABELS live)

# The plumbing itself, in every preset, on the [live-plumbing] test case
# only: without the opt-in it skips (exit code 4 and the reason), with it it
# runs and passes.
add_test(
    NAME live_tests_skip_without_opt_in
    COMMAND
        ${CMAKE_COMMAND} "-DEXECUTABLE=$<TARGET_FILE:mov_live_tests>" "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}" -P
        "${CMAKE_CURRENT_SOURCE_DIR}/check_skip.cmake"
)
add_test(NAME live_tests_run_with_opt_in COMMAND mov_live_tests "[live-plumbing]" --warn NoAssertions)
set_tests_properties(live_tests_run_with_opt_in PROPERTIES ENVIRONMENT "MOV_LIVE_API=1")
