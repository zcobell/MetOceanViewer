// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The units of each provider's plan (docs/providers-design.md §5.2):
// CO-OPS windows and datums, USGS pages and the bind between its stages,
// NDBC sources and fallbacks for given `now`s.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "catch_strings.hpp"
#include "mov/core/provider/coops.hpp"
#include "mov/core/provider/ndbc.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/station.hpp"
#include "mov/fetch/paging.hpp"
#include "mov/fetch/plan/coops.hpp"
#include "mov/fetch/plan/ndbc.hpp"
#include "mov/fetch/plan/usgs.hpp"
#include "must.hpp"

namespace {

namespace core = mov::core;
namespace cu = mov::core::usgs;
namespace nd = mov::fetch::ndbc;
using mov::fetch::Error;
using mov::fetch::PageGuard;
using mov::fetch::PageSummary;
using mov::fetch::UnitLabel;
using mov::fetch::test::must;
using std::chrono::year;

core::Time utc(int y, unsigned m, unsigned d) {
  return core::Time{std::chrono::sys_days{year{y} / std::chrono::month{m} /
                                          std::chrono::day{d}}};
}

core::TimeRange range(core::Time a, core::Time b) {
  return *core::TimeRange::make(a, b);
}

// ---- NDBC -----------------------------------------------------------------

core::ndbc::StdMet stdmet(core::Time a, core::Time b) {
  return {.station = *core::StationId<core::provider::Ndbc>::make("42040"),
          .range = range(a, b)};
}

nd::Source historical(int y, bool months = false) {
  return nd::Historical{.year = year{y}, .months_when_absent = months};
}
nd::Source monthly(int y, unsigned m,
                   nd::MonthSpelling s = nd::MonthSpelling::primary) {
  return nd::Monthly{
      .year = year{y}, .month = std::chrono::month{m}, .spelling = s};
}
nd::Source latest(int y, unsigned m) {
  return nd::MonthlyLatest{.year = year{y}, .month = std::chrono::month{m}};
}
nd::Source realtime(int y) { return nd::Realtime{.year = year{y}}; }

}  // namespace

TEST_CASE("NDBC: a range of past years is one historical file a year",
          "[fetch][plan][ndbc]") {
  const auto now = utc(2026, 10, 9);
  CHECK(nd::initial(stdmet(utc(2020, 6, 1), utc(2023, 2, 1)), now) ==
        std::vector<nd::Source>{historical(2020), historical(2021),
                                historical(2022), historical(2023)});
}

TEST_CASE("NDBC: last year's file falls back to its months",
          "[fetch][plan][ndbc][regression][L16]") {
  const auto now = utc(2026, 2, 10);
  const auto request = stdmet(utc(2025, 10, 15), utc(2025, 12, 20));
  CHECK(nd::initial(request, now) ==
        std::vector<nd::Source>{historical(2025, true)});
  CHECK(nd::fallback(request, historical(2025, true)) ==
        std::vector<nd::Source>{monthly(2025, 10), monthly(2025, 11),
                                monthly(2025, 12)});
  CHECK(nd::fallback(request, historical(2024)).empty());
}

TEST_CASE("NDBC: the current year is its past months, then realtime",
          "[fetch][plan][ndbc][regression][L16]") {
  const auto now = utc(2026, 10, 9);
  CHECK(nd::initial(stdmet(utc(2025, 12, 1), utc(2026, 10, 9)), now) ==
        std::vector<nd::Source>{
            historical(2025, true), monthly(2026, 1), monthly(2026, 2),
            monthly(2026, 3), monthly(2026, 4), monthly(2026, 5),
            monthly(2026, 6), monthly(2026, 7), monthly(2026, 8),
            monthly(2026, 9), realtime(2026)});
}

TEST_CASE("NDBC: realtime only when the range reaches the last 45 days",
          "[fetch][plan][ndbc]") {
  const auto now = utc(2026, 10, 9);
  CHECK(nd::initial(stdmet(utc(2026, 10, 1), utc(2026, 10, 5)), now) ==
        std::vector<nd::Source>{realtime(2026)});
  CHECK(nd::initial(stdmet(utc(2026, 3, 1), utc(2026, 4, 1)), now) ==
        std::vector<nd::Source>{monthly(2026, 3)});
  // A range entirely after now has nothing to fetch.
  CHECK(nd::initial(stdmet(utc(2026, 11, 1), utc(2026, 12, 1)), now).empty());
  CHECK(nd::initial(stdmet(now, utc(2026, 12, 1)), now).empty());
}

TEST_CASE("NDBC: a month falls back through its spellings to the latest file",
          "[fetch][plan][ndbc]") {
  const auto request = stdmet(utc(2026, 1, 1), utc(2026, 12, 1));
  CHECK(nd::fallback(request, monthly(2026, 3)) ==
        std::vector<nd::Source>{latest(2026, 3)});
  CHECK(nd::fallback(request, monthly(2026, 10)) ==
        std::vector<nd::Source>{monthly(2026, 10, nd::MonthSpelling::decimal)});
  CHECK(nd::fallback(request, monthly(2026, 10, nd::MonthSpelling::decimal)) ==
        std::vector<nd::Source>{latest(2026, 10)});
  CHECK(nd::fallback(request, latest(2026, 3)).empty());
  CHECK(nd::fallback(request, realtime(2026)).empty());
}

TEST_CASE("NDBC labels name the file", "[fetch][plan][ndbc]") {
  using core::ndbc::FileKind;
  using mov::fetch::NdbcFile;
  CHECK(nd::label(historical(2024)) == NdbcFile{.kind = FileKind::historical,
                                                .year = year{2024},
                                                .month = std::nullopt});
  CHECK(nd::label(monthly(2026, 3)) == NdbcFile{.kind = FileKind::monthly,
                                                .year = year{2026},
                                                .month = std::chrono::March});
  CHECK(nd::label(latest(2026, 3)) == NdbcFile{.kind = FileKind::monthly_latest,
                                               .year = year{2026},
                                               .month = std::chrono::March});
  CHECK(nd::label(realtime(2026)) == NdbcFile{.kind = FileKind::realtime,
                                              .year = year{2026},
                                              .month = std::nullopt});
}

// ---- CO-OPS ---------------------------------------------------------------

TEST_CASE("CO-OPS units are the request's windows", "[fetch][plan][coops]") {
  namespace cc = core::coops;
  const cc::Request request{
      .station = *core::StationId<core::provider::Coops>::make("8729840"),
      .range = range(utc(2026, 1, 1), utc(2026, 1, 9)),
      .product = cc::WaterLevel{
          .kind = cc::ObservedLevel{.series = cc::Observed::one_minute},
          .datum = cc::Datum::navd88}};
  const auto units = mov::fetch::coops::initial(request);
  REQUIRE(units);
  REQUIRE(units->size() == 2);
  CHECK(mov::fetch::coops::label((*units)[1]) ==
        UnitLabel{mov::fetch::Window{
            .range = range(utc(2026, 1, 5), utc(2026, 1, 9))}});
}

TEST_CASE("the CO-OPS datums stage is one unit per station",
          "[fetch][plan][coops]") {
  using Id = core::StationId<core::provider::Coops>;
  const mov::fetch::coops::DatumsRequest request{
      .stations = {*Id::make("8729840"), *Id::make("8761724")}};
  const auto units = mov::fetch::coops::initial(request);
  REQUIRE(units.size() == 2);
  CHECK(units[1].station == *Id::make("8761724"));
  CHECK(mov::fetch::coops::label(units[1]) ==
        UnitLabel{mov::fetch::CatalogPart{.index = 1}});
}

// ---- paging -----------------------------------------------------------------

namespace {

using Guard = PageGuard<std::string, core::Time>;
using Summary = PageSummary<std::string, core::Time>;

Summary page(std::size_t rows, std::optional<int> first,
             std::optional<int> last, std::optional<std::string> next) {
  const auto t = [](std::optional<int> s) -> std::optional<core::Time> {
    if (not s) {
      return std::nullopt;
    }
    return core::Time{std::chrono::seconds{*s}};
  };
  return {.rows = rows,
          .first = t(first),
          .last = t(last),
          .next = std::move(next)};
}

std::string_view refusal(const auto& result) {
  return result.error().context();
}

}  // namespace

TEST_CASE("a page chain follows next links and ends without one",
          "[fetch][paging]") {
  const Guard g = Guard::first(5);
  CHECK(g.taken() == 1);
  const auto end = g.after(page(3, 0, 10, std::nullopt));
  REQUIRE(end);
  CHECK_FALSE(*end);
  const auto turn = g.after(page(3, 0, 10, "p2"));
  REQUIRE(turn);
  const auto& taken = must(*turn);
  CHECK(taken.link == "p2");
  CHECK(taken.guard.taken() == 2);
  // An empty last page ends the chain.
  CHECK(g.after(page(0, std::nullopt, std::nullopt, std::nullopt)).has_value());
}

TEST_CASE(
    "a page chain refuses empty pages with a link, cycles, stalls and runaways",
    "[fetch][paging][regression]") {
  const Guard g = Guard::first(3);
  CHECK(refusal(g.after(page(0, std::nullopt, std::nullopt, "p2"))) ==
        "empty page with a next link");
  const auto second = must(*g.after(page(3, 0, 10, "p2")));
  CHECK(refusal(second.guard.after(page(3, 20, 30, "p2"))) ==
        "next link repeated");
  CHECK(refusal(second.guard.after(page(3, 10, 30, "p3"))) ==
        "page does not advance");
  CHECK(refusal(second.guard.after(page(3, 5, 30, std::nullopt))) ==
        "page does not advance");
  const auto third = must(*second.guard.after(page(3, 11, 20, "p3")));
  CHECK(third.guard.taken() == 3);
  const auto capped = third.guard.after(page(3, 21, 30, "p4"));
  CHECK(refusal(capped) == "page limit reached");
  CHECK(capped.error().code() == mov::io::ParseErrc::corrupt_record);
  CHECK(capped.error().line() == 3);
  // Rows without positions (a site's series) skip the advance rule.
  CHECK(second.guard.after(page(3, std::nullopt, std::nullopt, "p3"))
            .has_value());
}

// ---- USGS -------------------------------------------------------------------

namespace {

using Link = std::string;

cu::SeriesInfo series(std::string_view id, std::string_view parameter,
                      std::string_view statistic, bool primary) {
  return {.id = *cu::TimeSeriesId::make(id),
          .parameter = *cu::ParameterCode::make(parameter),
          .statistic = *cu::StatisticId::make(statistic),
          .parameter_name = "x",
          .primary = primary,
          .unit = std::nullopt,
          .validity = {}};
}

constexpr std::string_view id_a = "0123456789abcdef0123456789abcdef";
constexpr std::string_view id_b = "1123456789abcdef0123456789abcdef";
constexpr std::string_view id_c = "2123456789abcdef0123456789abcdef";

cu::Continuous continuous(core::TimeRange r) {
  return {
      .station = *core::StationId<core::provider::Usgs>::make("USGS-02323500"),
      .series = cu::ByParameter{.parameter = *cu::ParameterCode::make("00065")},
      .range = r};
}

cu::Daily daily_by_id(std::string_view id) {
  return {
      .station = *core::StationId<core::provider::Usgs>::make("USGS-02323500"),
      .series = cu::ByTimeSeries{.id = *cu::TimeSeriesId::make(id)},
      .days =
          *core::DayRange::make(std::chrono::local_days{year{2026} / 1 / 1},
                                std::chrono::local_days{year{2026} / 2 / 1})};
}

}  // namespace

TEST_CASE("the USGS bind resolves the site's series into the data stage",
          "[fetch][plan][usgs]") {
  const auto r = range(utc(2026, 1, 1), utc(2026, 2, 1));
  const auto bound = mov::fetch::usgs::bind(
      continuous(r), {series(id_a, "00065", "00011", true),
                      series(id_b, "00065", "00011", false),
                      series(id_c, "00065", "00003", true)});
  REQUIRE(bound);
  CHECK(bound->series.series().id == *cu::TimeSeriesId::make(id_a));
  CHECK(bound->request == continuous(r));

  const auto by_id = mov::fetch::usgs::bind(
      daily_by_id(id_c), {series(id_c, "00065", "00003", true)});
  REQUIRE(by_id);
  CHECK(by_id->series.series().statistic == cu::StatisticId::mean());
}

TEST_CASE("the USGS bind's failures are Resolution failures",
          "[fetch][plan][usgs]") {
  using mov::fetch::Failure;
  using mov::fetch::Resolution;
  const auto r = range(utc(2026, 1, 1), utc(2026, 2, 1));
  // Two Primary series: AmbiguousSeries with both ids, sorted.
  const auto ambiguous = mov::fetch::usgs::bind(
      continuous(r), {series(id_b, "00065", "00011", true),
                      series(id_a, "00065", "00011", true)});
  CHECK(ambiguous.error() ==
        Failure{.error =
                    mov::fetch::AmbiguousSeries{
                        .candidates = {*cu::TimeSeriesId::make(id_a),
                                       *cu::TimeSeriesId::make(id_b)}},
                .unit = Resolution{.page = 0}});
  // No Primary series: NoData.
  CHECK(mov::fetch::usgs::bind(continuous(r),
                               {series(id_a, "00065", "00011", false)})
            .error()
            .error == Error{mov::fetch::NoData{}});
  // A daily request naming a continuous series: WrongCollection.
  CHECK(mov::fetch::usgs::bind(daily_by_id(id_a),
                               {series(id_a, "00065", "00011", true)})
            .error()
            .error ==
        Error{mov::fetch::WrongCollection{.requested = cu::Collection::daily}});
  // A repeated id: the rows are corrupt.
  const auto repeated = mov::fetch::usgs::bind(
      continuous(r), {series(id_a, "00065", "00011", true),
                      series(id_a, "00065", "00011", false)});
  REQUIRE_FALSE(repeated);
  CHECK(std::get<mov::io::ParseError>(repeated.error().error).code() ==
        mov::io::ParseErrc::corrupt_record);
}

TEST_CASE("USGS continuous units are windows of 1000 days, first pages",
          "[fetch][plan][usgs]") {
  namespace mu = mov::fetch::usgs;
  const auto r = range(utc(2020, 1, 1), utc(2026, 1, 1));
  const auto bound =
      mu::bind(continuous(r), {series(id_a, "00065", "00011", true)});
  REQUIRE(bound);
  const auto units = mu::initial<Link>(*bound);
  REQUIRE(units);
  REQUIRE(units->size() == 3);  // 2192 days
  CHECK_FALSE((*units)[0].page.link);
  CHECK(mu::label((*units)[1]) ==
        UnitLabel{mov::fetch::Page{.window = (*units)[1].window, .index = 0}});
  // A page with a next link continues the window.
  const auto next = mu::next(
      (*units)[0], PageSummary<Link, core::Time>{.rows = 2,
                                                 .first = utc(2020, 1, 1),
                                                 .last = utc(2020, 2, 1),
                                                 .next = "p2"});
  REQUIRE(next);
  REQUIRE(next->size() == 1);
  CHECK((*next)[0].page.link == "p2");
  CHECK((*next)[0].window == (*units)[0].window);
  CHECK(mu::label((*next)[0]) ==
        UnitLabel{mov::fetch::Page{.window = (*units)[0].window, .index = 1}});
  // A refused page fails the unit with the ParseError.
  const auto refused =
      mu::next((*units)[0], PageSummary<Link, core::Time>{.rows = 0,
                                                          .first = std::nullopt,
                                                          .last = std::nullopt,
                                                          .next = "p2"});
  REQUIRE_FALSE(refused);
  CHECK(std::holds_alternative<mov::io::ParseError>(refused.error()));
  CHECK(
      mu::next((*units)[0], PageSummary<Link, core::Time>{.rows = 1,
                                                          .first = std::nullopt,
                                                          .last = std::nullopt,
                                                          .next = std::nullopt})
          ->empty());
}

TEST_CASE("USGS daily values are one unit; resolution pages are bounded",
          "[fetch][plan][usgs]") {
  namespace mu = mov::fetch::usgs;
  const auto bound =
      mu::bind(daily_by_id(id_c), {series(id_c, "00065", "00003", true)});
  REQUIRE(bound);
  const auto units = mu::initial<Link>(*bound);
  REQUIRE(units.size() == 1);
  CHECK(mu::label(units[0]) ==
        UnitLabel{mov::fetch::DayWindow{.days = units[0].days}});
  const auto day = [](unsigned d) {
    return std::chrono::local_days{year{2026} / std::chrono::January /
                                   std::chrono::day{d}};
  };
  const auto next = mu::next(
      units[0], PageSummary<Link, std::chrono::local_days>{
                    .rows = 3, .first = day(1), .last = day(3), .next = "d2"});
  REQUIRE(next);
  CHECK((*next)[0].days == units[0].days);
  CHECK(
      mu::next(units[0],
               PageSummary<Link, std::chrono::local_days>{.rows = 0,
                                                          .first = std::nullopt,
                                                          .last = std::nullopt,
                                                          .next = std::nullopt})
          ->empty());

  auto unit = mu::first_resolution_page<Link>();
  CHECK(mu::label(unit) == UnitLabel{mov::fetch::Resolution{.page = 0}});
  for (std::uint32_t taken = 1; taken < mu::max_resolution_pages; ++taken) {
    const auto more = mu::next(unit, PageSummary<Link, core::Time>{
                                         .rows = 1,
                                         .first = std::nullopt,
                                         .last = std::nullopt,
                                         .next = "r" + std::to_string(taken)});
    REQUIRE(more);
    REQUIRE(more->size() == 1);
    unit = (*more)[0];
  }
  CHECK(mu::label(unit) == UnitLabel{mov::fetch::Resolution{.page = 3}});
  CHECK_FALSE(
      mu::next(unit, PageSummary<Link, core::Time>{.rows = 1,
                                                   .first = std::nullopt,
                                                   .last = std::nullopt,
                                                   .next = "r9"})
          .has_value());
  CHECK(mu::next(unit, PageSummary<Link, core::Time>{.rows = 1,
                                                     .first = std::nullopt,
                                                     .last = std::nullopt,
                                                     .next = std::nullopt})
            ->empty());
}
