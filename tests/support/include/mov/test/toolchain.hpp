// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Standard-library gaps that the tests must tolerate (macOS libc++, MSVC STL).
// Each gate below names the library versions behind it.
// A check that a library cannot constant-evaluate runs at run time there
// instead, so coverage of the behaviour stays; every other toolchain keeps the
// compile-time check.

#pragma once

#include <version>

// A std::variant with a non-trivially-destructible alternative (a std::string
// inside Unit and QuantityId) can live in a constant expression only with a
// constexpr variant destructor (P2231R1, __cpp_lib_variant 202106L). Apple
// libc++ 16 and 17 (LLVM 17 to 19) lack it, so a constexpr Unit or QuantityId
// is ill-formed there.
#if defined(__cpp_lib_variant) && __cpp_lib_variant >= 202106L
#define MOV_TEST_CONSTEXPR_VARIANT 1
#else
#define MOV_TEST_CONSTEXPR_VARIANT 0
#endif

// The declaration keyword for a variable that holds such a variant.
#if MOV_TEST_CONSTEXPR_VARIANT
#define MOV_CONSTEXPR_VARIANT constexpr
#define MOV_STATIC_REQUIRE_VARIANT(...) STATIC_REQUIRE(__VA_ARGS__)
#define MOV_STATIC_REQUIRE_FALSE_VARIANT(...) STATIC_REQUIRE_FALSE(__VA_ARGS__)
#else
#define MOV_CONSTEXPR_VARIANT const
#define MOV_STATIC_REQUIRE_VARIANT(...) REQUIRE(__VA_ARGS__)
#define MOV_STATIC_REQUIRE_FALSE_VARIANT(...) REQUIRE_FALSE(__VA_ARGS__)
#endif
