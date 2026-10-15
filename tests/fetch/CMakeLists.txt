# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

mov_add_test(
    mov_fetch_tests
    SOURCES
        test_error.cpp
        test_machine.cpp
        test_machine_properties.cpp
        test_ndbc_machine.cpp
        test_part.cpp
        test_plans.cpp
        test_retry_after.cpp
    CONSTEXPR_SOURCES test_plan_constexpr.cpp test_policy_constexpr.cpp
    LIBRARIES mov::fetch
)

# The machine driven from bytes (docs/providers-design.md §10.4).
mov_add_fuzz_test(fuzz_fetch_machine SOURCES fuzz_fetch_machine.cpp LIBRARIES mov::fetch CORPUS fetch/machine)
