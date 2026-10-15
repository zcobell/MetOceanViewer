// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The NDBC plan in the machine: 404s walk the fallbacks, FellBack warnings
// name each step, and the parts merge in precedence order
// (docs/providers-design.md §5.2, §5.4).

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <expected>
#include <utility>
#include <variant>
#include <vector>

#include "machine_script.hpp"
#include "mov/core/provider/ndbc.hpp"
#include "mov/core/station.hpp"
#include "mov/fetch/machine.hpp"
#include "mov/fetch/plan/ndbc.hpp"

namespace {

namespace core = mov::core;
namespace nd = mov::fetch::ndbc;
using namespace mov::fetch;
using mov::fetch::test::Script;
using std::chrono::year;
using Values = std::vector<nd::Source>;

/// A stand-in for P7's NDBC spec: the plan is fetch's, the part the file.
struct NdbcSpec {
  using Request = core::ndbc::StdMet;
  using Unit = nd::Source;
  using Target = nd::Source;
  using Parsed = nd::Source;
  using Part = Values;
  using Value = Values;

  static std::expected<std::vector<Unit>, Error> initial(const Request& r,
                                                         core::Time now) {
    return nd::initial(r, now);
  }
  static std::vector<Target> targets(const Request& /*r*/, const Unit& u) {
    return {u};
  }
  static Origin origin(const Target& /*t*/) { return Origin::ndbc; }
  static UnitLabel label(const Request& /*r*/, const Unit& u) {
    return nd::label(u);
  }
  static std::expected<std::vector<Unit>, Error> next(
      const Request& r, const Unit& u, const UnitOutcome<Parsed>& outcome) {
    if (std::holds_alternative<Absent>(outcome)) {
      return nd::fallback(r, u);
    }
    return std::vector<Unit>{};
  }
  static Part into_part(std::vector<SlotResult<Parsed>> slots) {
    Part files;
    for (auto& s : slots) {
      if (auto* f = std::get_if<nd::Source>(&s)) {
        files.push_back(*f);
      }
    }
    return files;
  }
  static bool has_data(const Part& p) { return not p.empty(); }
  static std::expected<mov::io::Read<Value>, Error> merge(
      std::vector<Part> parts) {
    Value all;
    for (auto& p : parts) {
      all.insert(all.end(), p.begin(), p.end());
    }
    return mov::io::Read<Value>{.value = std::move(all), .warnings = {}};
  }
};
static_assert(FetchSpec<NdbcSpec>);

core::Time utc(int y, unsigned m, unsigned d) {
  return core::Time{std::chrono::sys_days{year{y} / std::chrono::month{m} /
                                          std::chrono::day{d}}};
}

}  // namespace

TEST_CASE("NDBC fallbacks run through the machine",
          "[fetch][machine][ndbc][regression][L16]") {
  const core::ndbc::StdMet request{
      .station = *core::StationId<core::provider::Ndbc>::make("42040"),
      .range = *core::TimeRange::make(utc(2025, 10, 15), utc(2026, 2, 5))};
  Script<NdbcSpec> sc{request, Options{}, utc(2026, 2, 10)};
  const auto sends = sc.sends();
  REQUIRE(sends.size() == 3);  // h2025, January 2026, realtime
  std::vector<Send<nd::Source>> queue{sends.begin(), sends.end()};
  // Last year's file and its finalized months are absent; the latest files,
  // January and realtime have data.
  while (not queue.empty()) {
    const auto s = queue.front();
    queue.erase(queue.begin());
    const bool absent =
        std::holds_alternative<nd::HistoricalLastYear>(s.target) or
        std::holds_alternative<nd::MonthlyDecimal>(s.target) or
        (std::holds_alternative<nd::Monthly>(s.target) and
         std::get<nd::Monthly>(s.target).year == year{2025});
    if (absent) {
      sc.respond(s.id, 404);
    } else {
      sc.respond(s.id, 200);
      sc.parsed(s.id, s.target);
    }
    for (const auto& n : sc.sends()) {
      queue.push_back(n);
    }
  }
  Values merged;
  for (const auto& p : sc.merge().in_plan_order) {
    merged.insert(merged.end(), p.begin(), p.end());
  }
  const auto latest = [](unsigned m) -> nd::Source {
    return nd::MonthlyLatest{.year = year{2025},
                             .month = std::chrono::month{m}};
  };
  CHECK(merged ==
        Values{latest(10), latest(11), latest(12),
               nd::Monthly{.year = year{2026}, .month = std::chrono::January},
               nd::Realtime{.year = year{2026}}});
  sc.merged(merged);
  const auto result = sc.finish().result;
  REQUIRE(result);
  std::vector<FellBack> fell_back;
  for (const auto& w : result->trail.warnings) {
    if (const auto* f = std::get_if<FetchWarning>(&w)) {
      if (const auto* back = std::get_if<FellBack>(f)) {
        fell_back.push_back(*back);
      }
    }
  }
  // h2025 to its three months, then October to December (after the
  // decimal spelling, which keeps the label) to the latest files.
  const NdbcFile h2025 = HistoricalFile{.year = year{2025}};
  const auto month = [](unsigned m) -> NdbcFile {
    return MonthlyFile{.year = year{2025}, .month = std::chrono::month{m}};
  };
  const auto latest_file = [](unsigned m) -> NdbcFile {
    return LatestMonthFile{.year = year{2025}, .month = std::chrono::month{m}};
  };
  CHECK(fell_back == std::vector<FellBack>{
                         {.from = h2025, .to = month(10)},
                         {.from = h2025, .to = month(11)},
                         {.from = h2025, .to = month(12)},
                         {.from = month(10), .to = latest_file(10)},
                         {.from = month(11), .to = latest_file(11)},
                         {.from = month(12), .to = latest_file(12)},
                     });
}
