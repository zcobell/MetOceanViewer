// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// How fast the text readers go. Hidden from the normal run (the "." tag) and
// meant for a release build:
//   mov_io_model_text_tests "[.throughput]"
// It prints MB/s and checks only that the results are right.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "adcirc_test_support.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/units.hpp"
#include "mov/io/adcirc_ascii.hpp"
#include "mov/io/hwm_file.hpp"
#include "mov/io/read_limits.hpp"

namespace {

using Clock = std::chrono::steady_clock;

double megabytes_per_second(std::size_t bytes, Clock::duration elapsed) {
  const double seconds = std::chrono::duration<double>(elapsed).count();
  return static_cast<double>(bytes) / 1e6 / seconds;
}

std::vector<mov::core::FileStation> stations(std::size_t n) {
  std::vector<mov::core::FileStation> out;
  out.reserve(n);
  const auto where = *mov::core::Location::make({.lat = 29.0, .lon = -90.0});
  for (std::size_t i = 0; i < n; ++i) {
    out.push_back({.id = *mov::core::StationKey::make(std::to_string(i)),
                   .name = {},
                   .location = where,
                   .native = std::nullopt,
                   .source = std::nullopt});
  }
  return out;
}

// fort.61 in the width ADCIRC writes.
std::string elevation_file(std::size_t station_count, std::size_t records) {
  std::string text = std::format("run\n{:10}{:11}  6.0000000E+02     1     1\n",
                                 records, station_count);
  for (std::size_t r = 0; r < records; ++r) {
    text += std::format("    {}.0000000000E+002{:13}\n", r + 1, r + 1);
    for (std::size_t s = 0; s < station_count; ++s) {
      text += std::format("{:10}   {}.2345678901E-001\n", s + 1, (r + s) % 9);
    }
  }
  return text;
}

}  // namespace

TEST_CASE("throughput: fort.61, 3000 stations, 3 or all selected",
          "[.throughput]") {
  constexpr std::size_t station_count = 3000;
  constexpr std::size_t records = 400;
  const std::string text = elevation_file(station_count, records);
  const auto all = stations(station_count);
  const mov::io::ReadContext ctx{};

  for (const std::size_t selected : {std::size_t{3}, station_count}) {
    std::vector<std::size_t> picked;
    picked.reserve(selected);
    for (std::size_t i = 0; i < selected; ++i) {
      picked.push_back(i * (station_count / selected));
    }
    auto selection = mov::core::StationSelection::make(picked, station_count);
    REQUIRE(selection.has_value());
    const auto start = Clock::now();
    const auto result =
        mov::io::parse_adcirc_ascii(text, all,
                                    {.kind = mov::io::AdcircKind::elevation,
                                     .cold_start = mov::test::cold_start(),
                                     .stations = *selection},
                                    ctx);
    const auto elapsed = Clock::now() - start;
    REQUIRE(result.has_value());
    CHECK(result->value.size() == selected);
    std::cout << std::format(
        "fort.61 {} MB, {} of {} stations: {:.0f} MB/s ({:.0f} ms)\n",
        text.size() / 1'000'000, selected, station_count,
        megabytes_per_second(text.size(), elapsed),
        std::chrono::duration<double, std::milli>(elapsed).count());
  }
}

TEST_CASE("throughput: HWM file, 300000 marks", "[.throughput]") {
  std::string text;
  for (std::size_t i = 0; i < 300'000; ++i) {
    text += std::format("-90.{:05},29.{:05},0.50,{}.25,{}.75,0.50\n", i % 99999,
                        (i * 7) % 99999, i % 9, i % 9);
  }
  const mov::io::ReadContext ctx{};
  const auto start = Clock::now();
  const auto result =
      mov::io::parse_hwm_csv(text, mov::core::LengthUnit::meter, ctx);
  const auto elapsed = Clock::now() - start;
  REQUIRE(result.has_value());
  CHECK(result->value.size() == 300'000);
  std::cout << std::format(
      "HWM {} MB, {} marks: {:.0f} MB/s ({:.0f} ms)\n", text.size() / 1'000'000,
      result->value.size(), megabytes_per_second(text.size(), elapsed),
      std::chrono::duration<double, std::milli>(elapsed).count());
}
