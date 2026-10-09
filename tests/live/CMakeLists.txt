# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# Label live: never run by a test preset (docs/providers-design.md §10.5).
mov_add_test(mov_live_opt_in LIVE SOURCES test_live_opt_in.cpp)

# The plumbing itself, in every preset: without the opt-in the live test
# skips (Catch2 exits 4 when every test case skipped, and prints the reason);
# with it, it runs and passes.
add_test(NAME live_tests_skip_without_opt_in COMMAND mov_live_opt_in)
set_tests_properties(
    live_tests_skip_without_opt_in
    PROPERTIES ENVIRONMENT_MODIFICATION "MOV_LIVE_API=unset:" PASS_REGULAR_EXPRESSION "set MOV_LIVE_API=1 to run it"
)
add_test(NAME live_tests_run_with_opt_in COMMAND mov_live_opt_in)
set_tests_properties(live_tests_run_with_opt_in PROPERTIES ENVIRONMENT "MOV_LIVE_API=1")
