// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The toolchain probe (docs/core-design.md section 1), compile-time half:
// one STATIC_REQUIRE per standard-library feature the core design gates on.
// A feature a compiler lacks fails the build of this file, which is the
// point: CI on every platform runs this probe, and the first failure names
// the feature to gate. Features whose availability is known to vary are
// guarded with their feature-test macro and SKIP, so the gap is visible in the
// test run instead of being silently ignored.
//
// Results on Linux (GCC 14.2 and Clang 20.1, both with libstdc++ 14):
//   fold_left, zip, chunk_by, enumerate, constexpr from_chars,
//   constexpr chrono calendar, constexpr expected monadic operations,
//   constexpr std::isfinite: available.
//   constexpr std::fabs and std::llround: GCC yes, Clang NO. The core
//   therefore uses its own helpers (mov/core/detail/numeric.hpp).
// Clang 18 with libc++ 18 (dev-libcxx preset; Apple Clang 16 stand-in): zip,
// chunk_by, constexpr from_chars, chrono calendar and expected monadic
// operations: available. fold_left, enumerate: missing (SKIP). std::isfinite:
// constexpr. Still to confirm on the Windows (MSVC STL) CI runner: every test
// below.

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <charconv>
#include <chrono>
#include <cmath>
#include <expected>
#include <functional>
#include <limits>
#include <numeric>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <vector>
#include <version>

namespace {

// ---- constexpr <cmath> (P0533) ----------------------------------------------
// Detected, not required: an ill-formed constant expression in a template
// argument is a substitution failure, so these are false rather than errors.

// The argument travels in a tag type, not a `template <double>` parameter:
// Apple Clang 16 does not support floating-point non-type template arguments.
template <double (*F)()>
struct Arg {
  static constexpr double value = F();
};
constexpr double one() { return 1.0; }
constexpr double two_and_a_half() { return 2.5; }
constexpr double minus_two_and_a_half() { return -2.5; }

template <class V>
inline constexpr bool llround_is_constexpr = requires {
  typename std::integral_constant<long long, std::llround(V::value)>;
};
template <class V>
inline constexpr bool fabs_is_constexpr = requires {
  typename std::integral_constant<bool, (std::fabs(V::value) > 1.0)>;
};
template <class V>
inline constexpr bool isfinite_is_constexpr = requires {
  typename std::integral_constant<bool, std::isfinite(V::value)>;
};

// Dependent on Available, so a library whose std::isfinite is not constexpr
// (MSVC STL) never compiles the call. True when there is nothing to check.
template <bool Available, class T>
constexpr bool isfinite_behaves() {
  if constexpr (Available) {
    return std::isfinite(T{1.5}) and
           not std::isfinite(std::numeric_limits<T>::infinity()) and
           not std::isfinite(std::numeric_limits<T>::quiet_NaN());
  } else {
    return true;
  }
}

// ---- ranges
// ---------------------------------------------------------------------

constexpr int dot_product() {
  constexpr std::array a{1, 2, 3};
  constexpr std::array b{4, 5, 6};
  int sum = 0;
  for (const auto [x, y] : std::views::zip(a, b)) {
    sum += x * y;
  }
  return sum;
}

#if defined(__cpp_lib_ranges_fold)
constexpr int folded_sum() {
  constexpr std::array a{1, 2, 3, 4};
  return std::ranges::fold_left(a, 0, std::plus<>{});
}
#endif

#if defined(__cpp_lib_ranges_chunk_by)
constexpr int run_count() {
  constexpr std::array a{1, 1, 2, 2, 2, 3};
  int runs = 0;
  for (const auto run : a | std::views::chunk_by(std::equal_to<>{})) {
    static_cast<void>(run);
    ++runs;
  }
  return runs;
}
#endif

#if defined(__cpp_lib_ranges_enumerate)
constexpr int weighted_sum() {
  constexpr std::array a{10, 20, 30};
  int sum = 0;
  for (const auto [index, value] : std::views::enumerate(a)) {
    sum += static_cast<int>(index) * value;
  }
  return sum;
}
#endif

// ---- everything else
// --------------------------------------------------------------

constexpr int parse_digits() {
  constexpr std::string_view text = "12345";
  int value = 0;
  std::from_chars(text.data(), text.data() + text.size(), value);
  return value;
}

constexpr long long days_since_epoch() {
  using namespace std::chrono;
  return sys_days{year{2005} / August / day{28}}.time_since_epoch().count();
}

constexpr bool ymd_validates() {
  using namespace std::chrono;
  return not year_month_day{year{2005} / February / day{29}}.ok() and
         year_month_day{year{2004} / February / day{29}}.ok();
}

constexpr int expected_chain() {
  const std::expected<int, int> ok = 20;
  const std::expected<int, int> bad = std::unexpected{7};
  return ok.transform([](int v) { return v + 1; })
             .and_then([](int v) -> std::expected<int, int> { return v * 2; })
             .value_or(0) +
         bad.transform_error([](int e) { return e + 1; }).error();
}

}  // namespace

TEST_CASE("probe: constexpr std::isfinite", "[core][probe][constexpr]") {
  constexpr bool available = isfinite_is_constexpr<Arg<one>>;
  STATIC_REQUIRE(isfinite_behaves<available, double>());  // vacuous if absent
  if constexpr (not available) {
    SKIP("std::isfinite is not constexpr: the core uses detail::is_finite");
  }
}

// The core avoids std::fabs and std::llround in constant expressions. This
// test only records whether the compiler could have used them.
TEST_CASE("probe: constexpr std::fabs and std::llround availability",
          "[core][probe]") {
  constexpr bool has_llround = llround_is_constexpr<Arg<two_and_a_half>>;
  constexpr bool has_fabs = fabs_is_constexpr<Arg<minus_two_and_a_half>>;
  if (not has_llround) {
    WARN(
        "constexpr std::llround is not available: the core uses "
        "detail::round_half_away");
  }
  if (not has_fabs) {
    WARN(
        "constexpr std::fabs is not available: the core uses "
        "detail::magnitude");
  }
  SUCCEED("recorded: llround " << has_llround << ", fabs " << has_fabs);
}

TEST_CASE("probe: views::zip", "[core][probe][constexpr]") {
  STATIC_REQUIRE(dot_product() == 32);
}

TEST_CASE("probe: ranges::fold_left", "[core][probe][constexpr]") {
#if defined(__cpp_lib_ranges_fold)
  STATIC_REQUIRE(folded_sum() == 10);
#else
  SKIP("ranges::fold_left is missing: ordered folds use std::accumulate");
#endif
}

TEST_CASE("probe: views::chunk_by", "[core][probe][constexpr]") {
#if defined(__cpp_lib_ranges_chunk_by)
  STATIC_REQUIRE(run_count() == 3);
#else
  SKIP("views::chunk_by is missing");
#endif
}

TEST_CASE("probe: views::enumerate", "[core][probe][constexpr]") {
#if defined(__cpp_lib_ranges_enumerate)
  STATIC_REQUIRE(weighted_sum() == 80);
#else
  SKIP("views::enumerate is missing: use a counter or views::zip with iota");
#endif
}

TEST_CASE("probe: constexpr integer from_chars", "[core][probe][constexpr]") {
#if defined(__cpp_lib_constexpr_charconv)
  STATIC_REQUIRE(parse_digits() == 12345);
#else
  SKIP("constexpr from_chars is missing: hand-written digit loops stay");
#endif
}

TEST_CASE("probe: constexpr chrono calendar", "[core][probe][constexpr]") {
  STATIC_REQUIRE(days_since_epoch() == 13023);
  STATIC_REQUIRE(ymd_validates());
}

TEST_CASE("probe: constexpr expected monadic operations",
          "[core][probe][constexpr]") {
  STATIC_REQUIRE(expected_chain() == 50);
}
