// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target for normalize. Every 3 bytes are one row: a signed time
// byte (a small range, so repeats and descents are common), a tag byte
// (Missing, Dry or a value) and a signed value byte. Oracle: the output is a
// valid series (strictly increasing), every input time appears once and
// carries the first sample given for it, the report counts are the ones
// computed independently here, and normalizing the output again changes
// nothing.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <utility>
#include <vector>

#include "mov/core/meta.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

using mov::core::Point;
using mov::core::Sample;
using mov::core::Time;

[[noreturn]] void fail() { std::abort(); }

void require(bool ok) {
  if (not ok) {
    fail();
  }
}

Sample sample_of(std::uint8_t tag, std::uint8_t value) {
  switch (tag % 3) {
    case 0:
      return Sample{mov::core::Missing{}};
    case 1:
      return Sample{mov::core::Dry{}};
    default:
      return mov::core::finite_or_missing(
          static_cast<double>(static_cast<std::int8_t>(value)));
  }
}

std::vector<Point> rows_of(const std::uint8_t* data, std::size_t size) {
  std::vector<Point> rows;
  for (std::size_t i = 0; i + 3 <= size; i += 3) {
    const auto ms = static_cast<std::int8_t>(data[i]);
    rows.push_back({.time = Time{std::chrono::milliseconds{ms}},
                    .sample = sample_of(data[i + 1], data[i + 2])});
  }
  return rows;
}

std::size_t descents_of(const std::vector<Point>& rows) {
  std::size_t n = 0;
  for (std::size_t i = 1; i < rows.size(); ++i) {
    n += rows[i].time < rows[i - 1].time ? 1 : 0;
  }
  return n;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::vector<Point> rows = rows_of(data, size);

  // The expected result: the first sample per time, and the conflicts.
  std::map<Time, Sample> first;
  std::size_t conflicts = 0;
  for (const Point& p : rows) {
    const auto [it, inserted] = first.try_emplace(p.time, p.sample);
    conflicts += (not inserted and it->second != p.sample) ? 1 : 0;
  }

  const mov::core::Normalized n =
      mov::core::normalize(rows, mov::core::SeriesMeta{});
  const mov::core::TimeSeries& s = n.series;

  require(mov::core::TimeSeries::make({s.times().begin(), s.times().end()},
                                      {s.samples().begin(), s.samples().end()},
                                      s.meta())
              .has_value());
  require(s.size() == first.size());
  std::size_t i = 0;
  for (const auto& [time, sample] : first) {
    require(s.times()[i] == time and s.samples()[i] == sample);
    ++i;
  }
  require(n.report.descents == descents_of(rows));
  require(n.report.duplicates_dropped == rows.size() - first.size());
  require(n.report.conflicting_duplicates == conflicts);
  require(n.report.clean() ==
          (n.report.descents == 0 and n.report.duplicates_dropped == 0));
  if (n.report.clean()) {
    require(std::ranges::equal(rows, s.points()));
  }

  std::vector<Point> again;
  for (const auto& [time, sample] : s.points()) {
    again.push_back({.time = time, .sample = sample});
  }
  const mov::core::Normalized twice =
      mov::core::normalize(std::move(again), mov::core::SeriesMeta{});
  require(twice.report.clean() and twice.series == s);
  return 0;
}
