// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Helpers shared by the ADCIRC ASCII and HWM file tests.

#pragma once

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/geo.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/io/error.hpp"
#include "mov/io/warning.hpp"
#include "mov/test/fixture.hpp"
#include "mov/test/scratch_dir.hpp"

namespace mov::test {

/// A committed fixture's bytes.
[[nodiscard]] inline std::string fixture_text(std::string_view relative) {
  const std::string text = read_bytes(fixture(relative));
  REQUIRE(not(text.empty()));
  return text;
}

[[nodiscard]] inline core::Epsg epsg(int code) {
  const auto made = core::Epsg::make(code);
  REQUIRE(made.has_value());
  return *made;
}

/// 2010-01-01 00:00:00 UTC, the cold start the ADCIRC tests use.
[[nodiscard]] inline core::Time cold_start() {
  using namespace std::chrono;
  return time_point_cast<milliseconds>(sys_days{year{2010} / January / 1});
}

[[nodiscard]] inline core::Time at_seconds(double seconds_after_cold_start) {
  using namespace std::chrono;
  return cold_start() + milliseconds{static_cast<std::int64_t>(
                            seconds_after_cold_start * 1000.0)};
}

/// The ParseError of a failed result; the test fails if it holds another
/// error.
template <class Result>
[[nodiscard]] const io::ParseError& parse_error_of(const Result& result) {
  REQUIRE(not(result.has_value()));
  const auto* error = std::get_if<io::ParseError>(&result.error());
  REQUIRE(error != nullptr);
  return *error;
}

template <class Result>
[[nodiscard]] const io::FormatError& format_error_of(const Result& result) {
  REQUIRE(not(result.has_value()));
  const auto* error = std::get_if<io::FormatError>(&result.error());
  REQUIRE(error != nullptr);
  return *error;
}

/// The warning with this code, or nullptr.
[[nodiscard]] inline const io::Warning* find_warning(
    const std::vector<io::Warning>& warnings, io::WarningCode code) {
  for (const io::Warning& w : warnings) {
    if (w.code == code) {
      return &w;
    }
  }
  return nullptr;
}

[[nodiscard]] inline std::size_t warning_count(
    const std::vector<io::Warning>& warnings, io::WarningCode code) {
  const io::Warning* w = find_warning(warnings, code);
  return w == nullptr ? 0 : w->count;
}

[[nodiscard]] inline core::Sample sample(double v) {
  const auto s = core::Sample::of(v);
  REQUIRE(s.has_value());
  return *s;
}

}  // namespace mov::test
