// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Helpers shared by the USGS wire-format tests: the recorded bodies and
// resolved series to read them as.

#pragma once

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/core/provider/usgs.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/core/windows.hpp"
#include "mov/io/read.hpp"
#include "mov/io/wire/usgs.hpp"
#include "mov/test/fixture.hpp"
#include "mov/test/scratch_dir.hpp"
#include "wire_test_support.hpp"

namespace mov::test {

/// A recorded USGS body (tests/fixtures/io/usgs/, provenance.txt there).
[[nodiscard]] inline std::string usgs_fixture(std::string_view name) {
  std::string text = read_bytes(fixture("io/usgs/" + std::string{name}));
  REQUIRE(not text.empty());
  return text;
}

/// The value of an expected the test requires to be there.
template <class E>
[[nodiscard]] auto must(E e) {
  REQUIRE(e.has_value());
  return *std::move(e);
}

/// The series that a recorded body belongs to, as resolution would find it.
struct SeriesFacts {
  std::string_view id = "66a4e3fb8abe42fcb0942b5c0fe98f68";
  std::string_view parameter = "00060";
  std::string_view statistic = "00011";
  std::string_view name = "Discharge";
  std::optional<std::string_view> unit = "ft^3/s";
};

[[nodiscard]] inline core::usgs::SeriesInfo series_info(SeriesFacts f) {
  return {.id = must(core::usgs::TimeSeriesId::make(f.id)),
          .parameter = must(core::usgs::ParameterCode::make(f.parameter)),
          .statistic = must(core::usgs::StatisticId::make(f.statistic)),
          .parameter_name = std::string{f.name},
          .primary = true,
          .unit = io::wire::usgs::unit_of_measure(f.unit).value,
          .validity = {}};
}

[[nodiscard]] inline core::StationId<core::provider::Usgs> usgs_station(
    std::string_view id = "USGS-01646500") {
  return must(core::StationId<core::provider::Usgs>::make(id));
}

/// The series of `facts` resolved for a continuous request by its id.
[[nodiscard]] inline core::usgs::Resolved<core::usgs::Continuous>
resolved_continuous(SeriesFacts facts = {}) {
  const core::usgs::SeriesInfo info = series_info(facts);
  const core::usgs::Continuous request{
      .station = usgs_station(),
      .series = core::usgs::ByTimeSeries{.id = info.id},
      .range = must(core::TimeRange::make(utc(2026, 10, 1), utc(2026, 10, 2)))};
  const auto set = must(core::usgs::SeriesSet::make({info}));
  return must(core::usgs::resolve(request, set));
}

/// The daily mean discharge of USGS-01646500 (or `facts`), resolved.
[[nodiscard]] inline core::usgs::Resolved<core::usgs::Daily> resolved_daily(
    SeriesFacts facts = {.id = "2cf409b0d9694c01828f5d4ff14586a5",
                         .statistic = "00003"}) {
  using namespace std::chrono;
  const core::usgs::SeriesInfo info = series_info(facts);
  const core::usgs::Daily request{
      .station = usgs_station(),
      .series = core::usgs::ByTimeSeries{.id = info.id},
      .days = must(core::DayRange::make(local_days{2026y / September / 1},
                                        local_days{2026y / October / 1}))};
  const auto set = must(core::usgs::SeriesSet::make({info}));
  return must(core::usgs::resolve(request, set));
}

/// The next link of a page, or "" when it has none.
[[nodiscard]] inline std::string next_url(const io::wire::usgs::Paging& p) {
  return p.next ? p.next->url() : std::string{};
}

/// A local day.
[[nodiscard]] inline std::chrono::local_days day(int y, unsigned m,
                                                 unsigned d) {
  using namespace std::chrono;
  return local_days{year{y} / month{m} / std::chrono::day{d}};
}

}  // namespace mov::test
