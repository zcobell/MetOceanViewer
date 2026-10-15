// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The measurements behind classify_body stopping at an unkept first member
// (docs/providers-design.md section 4.8, N6), and the SAX event counts of the
// 300k-element bodies of test_json_parse.cpp. Hidden (`[.measure]`); run from
// a release build:
//
//   mov_io_wire_tests "[measure][classify]"
//
// Each figure is the median of seven timed runs after one untimed run.

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <string>
#include <string_view>

#include "mov/io/detail/coops_json.hpp"
#include "mov/io/detail/json.hpp"
#include "mov/io/read_limits.hpp"
#include "wire_test_support.hpp"

namespace {

namespace json = mov::io::detail::json;
using Clock = std::chrono::steady_clock;

constexpr int timed_runs = 7;

// The median milliseconds of `run`, after one untimed run.
template <class F>
double median_ms(F run) {
  run();
  std::array<double, timed_runs> times{};
  for (double& t : times) {
    const auto start = Clock::now();
    run();
    t = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
  }
  std::ranges::sort(times);
  return times.at(timed_runs / 2);
}

// A station list of at least `bytes`: the recorded met list's stations,
// repeated.
std::string station_list(std::size_t bytes) {
  const std::string recorded = mov::test::coops_fixture("stations_met.json");
  const std::size_t open = recorded.find('[');
  const std::size_t close = recorded.rfind(']');
  const std::string stations = recorded.substr(open + 1, close - open - 1);
  std::string out = recorded.substr(0, open + 1);
  while (out.size() < bytes) {
    out += out.back() == '[' ? "" : ",";
    out += stations;
  }
  return out + recorded.substr(close);
}

}  // namespace

TEST_CASE(
    "classify_body on a 1.3 MB station list: stop at the first member "
    "or read it all",
    "[.measure][classify]") {
  const std::string body = station_list(std::size_t{1300} << 10);
  const json::Keep& keep = mov::io::detail::coops::classify_keep();
  std::uint64_t events_stop = 0;
  std::uint64_t events_all = 0;
  const double stop = median_ms([&] {
    events_stop = json::parse(body, keep, mov::io::ReadContext{}, {},
                              {.stop_at_unkept_first_member = true})
                      ->events;
  });
  const double all = median_ms([&] {
    events_all = json::parse(body, keep, mov::io::ReadContext{})->events;
  });
  std::cout << std::format(
      "classify {} bytes: stop at the first member {:.3f} ms ({} events), "
      "whole body {:.3f} ms ({} events)\n",
      body.size(), stop, events_stop, all, events_all);
  CHECK(events_stop < events_all);
}
