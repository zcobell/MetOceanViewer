# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

mov_add_test(
    mov_fetch_tests
    SOURCES
        test_error.cpp
        test_gate.cpp
        test_machine.cpp
        test_machine_properties.cpp
        test_ndbc_machine.cpp
        test_plans.cpp
        test_retry_after.cpp
        test_then.cpp
    CONSTEXPR_SOURCES test_plan_constexpr.cpp test_policy_constexpr.cpp
    LIBRARIES mov::fetch
)

# docs/providers-design.md §10.4: the machines driven from bytes (the toy
# spec, and USGS and CO-OPS through Then), the admission gate, and
# Retry-After.
mov_add_fuzz_test(fuzz_fetch_machine SOURCES fuzz_fetch_machine.cpp LIBRARIES mov::fetch CORPUS fetch/machine)
mov_add_fuzz_test(fuzz_fetch_gate SOURCES fuzz_fetch_gate.cpp LIBRARIES mov::fetch CORPUS fetch/gate)
mov_add_fuzz_test(fuzz_retry_after SOURCES fuzz_retry_after.cpp LIBRARIES mov::fetch CORPUS fetch/retry_after)
# A chain of USGS pages read by io and followed by the plan (§5.2, §10.4).
mov_add_fuzz_test(fuzz_usgs_paging SOURCES fuzz_usgs_paging.cpp LIBRARIES mov::fetch CORPUS fetch/usgs_paging)
