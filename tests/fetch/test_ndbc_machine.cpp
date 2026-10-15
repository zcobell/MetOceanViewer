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
using Values = std::vector<std::int64_t>;

/// A stand-in for P7's NDBC spec: the plan is fetch's, the part a number
/// per file.
struct NdbcSpec {
  using Request = core::ndbc::StdMet;
  using Unit = nd::Source;
  using Target = nd::Source;
  using Parsed = Values;
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
  static Part into_part(Parsed&& p) { return std::move(p); }
  static bool has_data(const Part& p) { return not p.empty(); }
};
static_assert(FetchSpec<NdbcSpec>);

core::Time utc(int y, unsigned m, unsigned d) {
  return core::Time{std::chrono::sys_days{year{y} / std::chrono::month{m} /
                                          std::chrono::day{d}}};
}

/// A number that names the file.
std::int64_t number(const nd::Source& s) {
  const auto l = nd::label(s);
  constexpr std::int64_t per_kind = 1000;
  constexpr std::int64_t per_year = 13;
  const std::int64_t month = l.month ? static_cast<unsigned>(*l.month) : 0;
  return (core::ndbc::precedence(l.kind) * per_kind) +
         ((static_cast<int>(l.year) - 2020) * per_year) + month;
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
  std::vector<FellBack> fell_back;
  std::vector<Send<nd::Source>> queue{sends.begin(), sends.end()};
  // Every historical and every primary or decimal monthly file is absent;
  // the latest files and realtime have data.
  while (not queue.empty()) {
    const auto s = queue.front();
    queue.erase(queue.begin());
    const bool absent = std::holds_alternative<nd::Historical>(s.target) or
                        (std::holds_alternative<nd::Monthly>(s.target) and
                         std::get<nd::Monthly>(s.target).year == year{2025});
    if (absent) {
      sc.respond(s.id, 404);
    } else {
      sc.respond(s.id, 200);
      sc.parsed(s.id, Values{number(s.target)});
    }
    for (const auto& n : sc.sends()) {
      queue.push_back(n);
    }
  }
  const auto merge = sc.merge();
  Values merged;
  for (const auto& p : merge.in_plan_order) {
    merged.insert(merged.end(), p.begin(), p.end());
  }
  // Oct (latest), Nov (latest), Dec (latest) 2025, January 2026, realtime.
  const auto latest = [](unsigned m) {
    return number(
        nd::MonthlyLatest{.year = year{2025}, .month = std::chrono::month{m}});
  };
  CHECK(merged ==
        Values{latest(10), latest(11), latest(12),
               number(nd::Monthly{.year = year{2026},
                                  .month = std::chrono::January,
                                  .spelling = nd::MonthSpelling::primary}),
               number(nd::Realtime{.year = year{2026}})});
  sc.merged(merged);
  const auto result = sc.finish().result;
  REQUIRE(result);
  for (const auto& w : result->warnings) {
    if (const auto* f = std::get_if<FetchWarning>(&w)) {
      fell_back.push_back(std::get<FellBack>(*f));
    }
  }
  // h2025 to its three months, then each month (after its decimal
  // spelling, which keeps the label) to its latest file, in plan order.
  using core::ndbc::FileKind;
  const auto file = [](FileKind k, std::optional<unsigned> m) {
    return NdbcFile{
        .kind = k,
        .year = year{2025},
        .month = m ? std::optional{std::chrono::month{*m}} : std::nullopt};
  };
  CHECK(fell_back == std::vector<FellBack>{
                         {.from = file(FileKind::historical, std::nullopt),
                          .to = file(FileKind::monthly, 10)},
                         {.from = file(FileKind::historical, std::nullopt),
                          .to = file(FileKind::monthly, 11)},
                         {.from = file(FileKind::historical, std::nullopt),
                          .to = file(FileKind::monthly, 12)},
                         {.from = file(FileKind::monthly, 10),
                          .to = file(FileKind::monthly_latest, 10)},
                         {.from = file(FileKind::monthly, 11),
                          .to = file(FileKind::monthly_latest, 11)},
                         {.from = file(FileKind::monthly, 12),
                          .to = file(FileKind::monthly_latest, 12)},
                     });
}
