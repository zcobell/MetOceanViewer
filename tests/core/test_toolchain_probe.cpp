// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The WP1 toolchain probe (docs/core-design.md section 1), run-time half. See
// test_toolchain_probe_constexpr.cpp for the compile-time half and the
// results recorded so far.

#include <catch2/catch_test_macros.hpp>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <version>

#include "mov/core/detail/numeric.hpp"

namespace {

// Values around every edge of the three helpers.
std::vector<double> edge_values() {
  const double inf = std::numeric_limits<double>::infinity();
  const double max = std::numeric_limits<double>::max();
  return {0.0,
          -0.0,
          0.25,
          0.4999999999999999,
          0.5,
          0.5000000000000001,
          1.5,
          2.5,
          -0.5,
          -1.5,
          -2.5,
          1234567.5,
          9007199254740991.0,
          -9007199254740991.0,
          4503599627370495.5,
          max,
          -max,
          std::numeric_limits<double>::denorm_min(),
          inf,
          -inf,
          std::numeric_limits<double>::quiet_NaN()};
}

}  // namespace

TEST_CASE("probe: core numeric helpers agree with <cmath>",
          "[core][probe][numeric]") {
  for (const double v : edge_values()) {
    INFO("value: " << v);
    CHECK(mov::core::detail::is_finite(v) == std::isfinite(v));
    if (not std::isnan(v)) {
      CHECK(mov::core::detail::magnitude(v) == std::fabs(v));
    }
    if (std::isfinite(v) and std::fabs(v) < 9.3e18) {
      CHECK(mov::core::detail::round_half_away(v) == std::llround(v));
    }
  }
  CHECK(std::isnan(
      mov::core::detail::magnitude(std::numeric_limits<double>::quiet_NaN())));
}

TEST_CASE("probe: floating-point from_chars", "[core][probe]") {
  // docs/core-design.md section 1: Apple libc++ may lack it; io::detail's
  // parse_double gates on __cpp_lib_to_chars and falls back to strtod_l.
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
  const std::string_view text = "-9.9999E+004";
  double value = 0.0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  CHECK(result.ec == std::errc{});
  CHECK(value == -99999.0);
#else
  SKIP("floating-point from_chars is missing: parse_double uses strtod_l");
#endif
}

TEST_CASE("probe: std::format of a millisecond time point", "[core][probe]") {
  using namespace std::chrono;
  const sys_time<milliseconds> t = sys_days{year{2005} / August / day{28}} +
                                   hours{12} + minutes{30} + seconds{45} +
                                   milliseconds{123};
  CHECK(std::format("{:%FT%T}Z", t) == "2005-08-28T12:30:45.123Z");
}

TEST_CASE("probe: stop_token", "[core][probe]") {
  std::stop_source source;
  const std::stop_token token = source.get_token();
  CHECK_FALSE(token.stop_requested());
  source.request_stop();
  CHECK(token.stop_requested());
}

// The core never calls std::chrono::parse (libc++ lacks it); this only
// records whether the library declares it.
TEST_CASE("probe: chrono::parse availability is recorded", "[core][probe]") {
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
  SUCCEED("__cpp_lib_chrono = " << __cpp_lib_chrono);
#else
  SUCCEED(
      "__cpp_lib_chrono is older than C++20 calendar and time zone support");
#endif
}
