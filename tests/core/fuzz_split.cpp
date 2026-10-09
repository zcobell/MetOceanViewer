// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: split and aligned, fed a raw begin, end, span and grain,
// must never overflow (UBSan in the fuzz preset), and their laws hold:
// split's view has ceil(length / span) windows, the first begins and the last
// ends with the range, and they are consecutive and `span` long but the last
// (checked window by window for views of up to 4096 windows, and around the
// ends and at random indices for larger ones); aligned contains the range,
// sits on the grain and widens each side by less than one grain, and refuses
// only when a bound has no multiple of the grain on the time line. A span
// or grain below 1 ms is no WindowSpan.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "mov/core/time.hpp"
#include "mov/core/windows.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

using mov::core::Time;
using mov::core::TimeRange;
using mov::core::Windows;
using mov::core::WindowSpan;
using std::chrono::milliseconds;

constexpr std::size_t input_size = 4 * sizeof(std::int64_t);
constexpr std::uint64_t checked_windows = 4096;
constexpr std::int64_t int64_max = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t int64_min = std::numeric_limits<std::int64_t>::min();

std::int64_t count(Time t) { return t.time_since_epoch().count(); }

std::uint64_t length_of(TimeRange r) {
  return static_cast<std::uint64_t>(count(r.end())) -
         static_cast<std::uint64_t>(count(r.begin()));
}

/// Window i is `step` long (or at most `step` when last) and meets window
/// i + 1.
void check_window(const Windows& w, std::uint64_t i, std::uint64_t step) {
  const TimeRange x = w[i];
  const bool last = i + 1 == w.size();
  if ((last and length_of(x) > step) or (not last and length_of(x) != step) or
      (not last and x.end() != w[i + 1].begin())) {
    std::abort();
  }
}

void check_split(TimeRange r, WindowSpan span) {
  const auto step = static_cast<std::uint64_t>(span.duration().count());
  const Windows w = mov::core::split(r, span);
  if (w.size() != ((length_of(r) - 1) / step) + 1 or
      w.front().begin() != r.begin() or w.back().end() != r.end()) {
    std::abort();
  }
  if (w.size() <= checked_windows) {
    for (std::uint64_t i = 0; i < w.size(); ++i) {
      check_window(w, i, step);
    }
    return;
  }
  for (std::uint64_t i = 0; i < 64; ++i) {
    check_window(w, i, step);
    check_window(w, w.size() - 1 - i, step);
    // A spread of indices, from the input.
    check_window(w, (length_of(r) ^ (i * 0x9e3779b97f4a7c15ULL)) % w.size(),
                 step);
  }
}

void check_aligned(TimeRange r, WindowSpan grain) {
  const auto a = mov::core::aligned(r, grain);
  const std::int64_t g = grain.duration().count();
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
  const auto split_span = WindowSpan::make(milliseconds{span});
  if (split_span.has_value() != (span >= 1)) {
    std::abort();
  }
  if (split_span) {
    check_split(*r, *split_span);
  }
  if (const auto align_grain = WindowSpan::make(milliseconds{grain})) {
    check_aligned(*r, *align_grain);
  }
  return 0;
}
