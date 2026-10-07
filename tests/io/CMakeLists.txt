# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

mov_add_test(
    mov_io_tests
    SOURCES
        test_netcdf_library.cpp
        test_error.cpp
        test_read.cpp
        test_read_chain.cpp
        test_text.cpp
        test_text_file.cpp
        test_atomic_file.cpp
        test_cf_time.cpp
        test_projection.cpp
    CONSTEXPR_SOURCES test_cf_time_constexpr.cpp
    LIBRARIES mov::io
)

# The locale-independence tests need de_DE.UTF-8 (the dev image and CI
# generate it). With the option on they fail, rather than skip, when it is
# missing; MOV_REQUIRE_LOCALES=1 in the environment does the same at run time.
if(MOV_REQUIRE_LOCALES)
    target_compile_definitions(mov_io_tests PRIVATE MOV_REQUIRE_LOCALES=1)
endif()
