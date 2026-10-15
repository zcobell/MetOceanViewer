// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Helpers shared by the wire-format tests (request targets, body checks,
// provider response parsers).

#pragma once

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "mov/core/sample.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/io/error.hpp"
#include "mov/io/warning.hpp"
#include "mov/test/fixture.hpp"
#include "mov/test/scratch_dir.hpp"

namespace mov::test {

/// A recorded CO-OPS body (tests/fixtures/io/coops/, provenance.txt there).
[[nodiscard]] inline std::string coops_fixture(std::string_view name) {
  std::string text = read_bytes(fixture("io/coops/" + std::string{name}));
  REQUIRE(not text.empty());
  return text;
}

/// The UTC time of a civil date and time. Summed in milliseconds: MSVC's
/// days, hours and minutes have a 32-bit rep, and the minutes of year 9999
/// do not fit one.
[[nodiscard]] inline core::Time utc(int y, unsigned mo, unsigned d,
                                    unsigned h = 0, unsigned mi = 0,
                                    unsigned s = 0) {
  using namespace std::chrono;
  const core::Time midnight =
      time_point_cast<milliseconds>(sys_days{year{y} / month{mo} / day{d}});
  return midnight + milliseconds{hours{h}} + milliseconds{minutes{mi}} +
         milliseconds{seconds{s}};
}

/// The ParseError of a failed result (a reference into it: copy it to keep
/// it past the result).
template <class Result>
[[nodiscard]] const io::ParseError& parse_error_in(const Result& result) {
  REQUIRE(not result.has_value());
  const auto* error = std::get_if<io::ParseError>(&result.error());
  REQUIRE(error != nullptr);
  return *error;
}

/// The FormatError of a failed result (whose error is an io::Error or a
/// FormatError).
template <class Result>
[[nodiscard]] io::FormatError format_error_in(const Result& result) {
  REQUIRE(not result.has_value());
  if constexpr (std::is_same_v<typename Result::error_type, io::FormatError>) {
    return result.error();
  } else {
    const auto* error = std::get_if<io::FormatError>(&result.error());
    REQUIRE(error != nullptr);
    return *error;
  }
}

/// The values of a series' samples, Missing as nullopt.
[[nodiscard]] inline std::vector<std::optional<double>> values_of(
    const core::TimeSeries& series) {
  std::vector<std::optional<double>> out;
  for (const core::Sample& s : series.samples()) {
    out.push_back(s.value());
  }
  return out;
}

/// The warning of `code`, if the list has one.
[[nodiscard]] inline std::optional<io::Warning> warning_of(
    const std::vector<io::Warning>& warnings, io::WarningCode code) {
  for (const io::Warning& w : warnings) {
    if (w.code == code) {
      return w;
    }
  }
  return std::nullopt;
}

}  // namespace mov::test
