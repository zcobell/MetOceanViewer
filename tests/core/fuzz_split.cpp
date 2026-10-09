// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: split and aligned, fed a raw begin, end, span and grain,
// must never overflow (UBSan in the fuzz preset), and their laws hold:
// split's windows are consecutive, cover the range and are `span` long but
// the last; aligned contains the range, sits on the grain and widens each
// side by less than one grain, and refuses only when a bound has no multiple
// of the grain on the time line. DayRange::covering agrees with the
// midnight test.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

#include "mov/core/time.hpp"
#include "mov/core/windows.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

using mov::core::Time;
using mov::core::TimeRange;
using std::chrono::milliseconds;

constexpr std::size_t input_size = 4 * sizeof(std::int64_t);
// Larger plans are legal but slow to check; a plan of windows is bounded
// by its caller (a user's range over a product's maximum window).
constexpr std::uint64_t max_windows = 4096;
constexpr std::int64_t int64_max = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t int64_min = std::numeric_limits<std::int64_t>::min();

std::int64_t count(Time t) { return t.time_since_epoch().count(); }

void check_split(TimeRange r, milliseconds span) {
  const std::uint64_t length = static_cast<std::uint64_t>(count(r.end())) -
                               static_cast<std::uint64_t>(count(r.begin()));
  const auto step = static_cast<std::uint64_t>(span.count());
  if ((length - 1) / step >= max_windows) {
    return;
  }
  const std::vector<TimeRange> w = mov::core::split(r, span);
  if (w.empty() or w.size() != ((length - 1) / step) + 1 or
      w.front().begin() != r.begin() or w.back().end() != r.end()) {
    std::abort();
  }
  for (std::size_t i = 0; i < w.size(); ++i) {
    const std::uint64_t window_length =
        static_cast<std::uint64_t>(count(w[i].end())) -
        static_cast<std::uint64_t>(count(w[i].begin()));
    const bool last = i + 1 == w.size();
    if ((last and window_length > step) or
        (not last and window_length != step) or
        (not last and w[i].end() != w[i + 1].begin())) {
      std::abort();
    }
  }
}

void check_aligned(TimeRange r, milliseconds grain) {
  const auto a = mov::core::aligned(r, grain);
  const std::int64_t g = grain.count();
  if (not a) {
    const bool low = count(r.begin()) < int64_min + g;
    const bool high = count(r.end()) > int64_max - g;
    if (not(low or high)) {
      std::abort();
    }
    return;
  }
  const bool on_grain = count(a->begin()) % g == 0 and count(a->end()) % g == 0;
  const bool contains = a->begin() <= r.begin() and r.end() <= a->end();
  // Each side moved by less than one grain (both differences are >= 0).
  const bool tight = static_cast<std::uint64_t>(count(r.begin())) -
                             static_cast<std::uint64_t>(count(a->begin())) <
                         static_cast<std::uint64_t>(g) and
                     static_cast<std::uint64_t>(count(a->end())) -
                             static_cast<std::uint64_t>(count(r.end())) <
                         static_cast<std::uint64_t>(g);
  if (not(on_grain and contains and tight)) {
    std::abort();
  }
}

void check_covering(TimeRange r) {
  constexpr std::int64_t ms_per_day = 86'400'000;
  const auto days = mov::core::DayRange::covering(r);
  const bool midnights =
      count(r.begin()) % ms_per_day == 0 and count(r.end()) % ms_per_day == 0;
  if (days.has_value() and not midnights) {
    std::abort();
  }
  if (days and
      (days->first() != std::chrono::floor<std::chrono::days>(r.begin()) or
       days->size().count() < 1)) {
    std::abort();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size < input_size) {
    return 0;
  }
  std::int64_t begin = 0;
  std::int64_t end = 0;
  std::int64_t span = 0;
  std::int64_t grain = 0;
  std::memcpy(&begin, data, sizeof begin);
  std::memcpy(&end, data + sizeof begin, sizeof end);
  std::memcpy(&span, data + (2 * sizeof begin), sizeof span);
  std::memcpy(&grain, data + (3 * sizeof begin), sizeof grain);
  const auto r =
      TimeRange::make(Time{milliseconds{begin}}, Time{milliseconds{end}});
  if (not r) {
    return 0;
  }
  if (span >= 1) {
    check_split(*r, milliseconds{span});
  }
  if (grain >= 1) {
    check_aligned(*r, milliseconds{grain});
  } else if (mov::core::aligned(*r, milliseconds{grain})) {
    std::abort();  // a grain below 1 ms aligns nothing
  }
  check_covering(*r);
  return 0;
}
