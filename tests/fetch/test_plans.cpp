// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The units of each provider's plan (docs/providers-design.md §5.2):
// CO-OPS windows and datums, USGS pages and the bind between its stages,
// NDBC sources and fallbacks for given `now`s.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <limits>
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
using mov::fetch::Extent;
using mov::fetch::NoPosition;
using mov::fetch::PageGuard;
using mov::fetch::PageSummary;
using mov::fetch::PagingError;
using mov::fetch::PagingRule;
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

nd::Source historical(int y) { return nd::Historical{.year = year{y}}; }
nd::Source last_year(int y) { return nd::HistoricalLastYear{.year = year{y}}; }
nd::Source monthly(int y, unsigned m) {
  return nd::Monthly{.year = year{y}, .month = std::chrono::month{m}};
}
nd::Source decimal(int y, nd::LateMonth m) {
  return nd::MonthlyDecimal{.year = year{y}, .month = m};
}
nd::Source latest(int y, unsigned m) {
  return nd::MonthlyLatest{.year = year{y}, .month = std::chrono::month{m}};
}
nd::Source realtime(int y) { return nd::Realtime{.year = year{y}}; }

std::vector<nd::Source> sources(const core::ndbc::StdMet& r, core::Time now) {
  auto planned = nd::initial(r, now);
  REQUIRE(planned);
  return *std::move(planned);
}

}  // namespace

TEST_CASE("NDBC: a range of past years is one historical file a year",
          "[fetch][plan][ndbc]") {
  const auto now = utc(2026, 10, 9);
  CHECK(sources(stdmet(utc(2020, 6, 1), utc(2023, 2, 1)), now) ==
        std::vector<nd::Source>{historical(2020), historical(2021),
                                historical(2022), historical(2023)});
}

TEST_CASE("NDBC: a range starts at the first year NDBC has",
          "[fetch][plan][ndbc]") {
  const auto now = utc(2026, 10, 9);
  CHECK(sources(stdmet(utc(1968, 6, 1), utc(1971, 2, 1)), now) ==
        std::vector<nd::Source>{historical(1970), historical(1971)});
  CHECK(sources(stdmet(utc(1900, 1, 1), utc(1960, 1, 1)), now).empty());
}

TEST_CASE("NDBC: a range beyond the nameable years is refused",
          "[fetch][plan][ndbc]") {
  const core::Time far{std::chrono::years{40'000}};
  const auto r = stdmet(utc(2020, 1, 1), far);
  CHECK(nd::initial(r, utc(2026, 10, 9)).error() ==
        Error{mov::fetch::UnsupportedRange{.range = r.range}});
}

TEST_CASE("NDBC: last year's file falls back to its months",
          "[fetch][plan][ndbc][regression][L16]") {
  const auto now = utc(2026, 2, 10);
  const auto request = stdmet(utc(2025, 10, 15), utc(2025, 12, 20));
  CHECK(sources(request, now) == std::vector<nd::Source>{last_year(2025)});
  CHECK(nd::fallback(request, last_year(2025)) ==
        std::vector<nd::Source>{monthly(2025, 10), monthly(2025, 11),
                                monthly(2025, 12)});
  CHECK(nd::fallback(request, historical(2024)).empty());
}

TEST_CASE("NDBC: the current year is its past months, then realtime",
          "[fetch][plan][ndbc][regression][L16]") {
  const auto now = utc(2026, 10, 9);
  CHECK(sources(stdmet(utc(2025, 12, 1), utc(2026, 10, 9)), now) ==
        std::vector<nd::Source>{
            last_year(2025), monthly(2026, 1), monthly(2026, 2),
            monthly(2026, 3), monthly(2026, 4), monthly(2026, 5),
            monthly(2026, 6), monthly(2026, 7), monthly(2026, 8),
            monthly(2026, 9), realtime(2026)});
}

TEST_CASE("NDBC: realtime only when the range reaches the last 45 days",
          "[fetch][plan][ndbc]") {
  const auto now = utc(2026, 10, 9);
  CHECK(sources(stdmet(utc(2026, 10, 1), utc(2026, 10, 5)), now) ==
        std::vector<nd::Source>{realtime(2026)});
  CHECK(sources(stdmet(utc(2026, 3, 1), utc(2026, 4, 1)), now) ==
        std::vector<nd::Source>{monthly(2026, 3)});
  CHECK(sources(stdmet(utc(2026, 11, 1), utc(2026, 12, 1)), now).empty());
  CHECK(sources(stdmet(now, utc(2026, 12, 1)), now).empty());
}

TEST_CASE("NDBC: a month falls back through its spellings to the latest file",
          "[fetch][plan][ndbc]") {
  const auto request = stdmet(utc(2026, 1, 1), utc(2026, 12, 1));
  CHECK(nd::fallback(request, monthly(2026, 3)) ==
        std::vector<nd::Source>{latest(2026, 3)});
  CHECK(nd::fallback(request, monthly(2026, 10)) ==
        std::vector<nd::Source>{decimal(2026, nd::LateMonth::october)});
  CHECK(nd::fallback(request, decimal(2026, nd::LateMonth::december)) ==
        std::vector<nd::Source>{latest(2026, 12)});
  CHECK(nd::fallback(request, latest(2026, 3)).empty());
  CHECK(nd::fallback(request, realtime(2026)).empty());
}

TEST_CASE("NDBC: a source's extent is the request within its year or month",
          "[fetch][plan][ndbc]") {
  const auto request = stdmet(utc(2025, 10, 15), utc(2026, 2, 5));
  CHECK(nd::extent(request, latest(2025, 12)) ==
        range(utc(2025, 12, 1), utc(2026, 1, 1)));
  CHECK(nd::extent(request, monthly(2025, 10)) ==
        range(utc(2025, 10, 15), utc(2025, 11, 1)));
  CHECK(nd::extent(request, decimal(2025, nd::LateMonth::november)) ==
        range(utc(2025, 11, 1), utc(2025, 12, 1)));
  CHECK(nd::extent(request, historical(2025)) ==
        range(utc(2025, 10, 15), utc(2026, 1, 1)));
  CHECK(nd::extent(request, last_year(2026)) ==
        range(utc(2026, 1, 1), utc(2026, 2, 5)));
  CHECK(nd::extent(request, realtime(2026)) == request.range);
  // A source outside the request keeps the request's range.
  CHECK(nd::extent(request, monthly(2024, 1)) == request.range);
}

TEST_CASE("NDBC labels name the file", "[fetch][plan][ndbc]") {
  using mov::fetch::HistoricalFile;
  using mov::fetch::LatestMonthFile;
  using mov::fetch::MonthlyFile;
  using mov::fetch::NdbcFile;
  using mov::fetch::RealtimeFile;
  CHECK(nd::label(historical(2024)) ==
        NdbcFile{HistoricalFile{.year = year{2024}}});
  CHECK(nd::label(last_year(2025)) ==
        NdbcFile{HistoricalFile{.year = year{2025}}});
  CHECK(nd::label(monthly(2026, 3)) ==
        NdbcFile{MonthlyFile{.year = year{2026}, .month = std::chrono::March}});
  CHECK(
      nd::label(decimal(2026, nd::LateMonth::october)) ==
      NdbcFile{MonthlyFile{.year = year{2026}, .month = std::chrono::October}});
  CHECK(nd::label(latest(2026, 3)) ==
        NdbcFile{
            LatestMonthFile{.year = year{2026}, .month = std::chrono::March}});
  CHECK(nd::label(realtime(2026)) ==
        NdbcFile{RealtimeFile{.year = year{2026}}});
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

TEST_CASE("a CO-OPS range at the end of the time line is Unalignable",
          "[fetch][plan][coops]") {
  namespace cc = core::coops;
  constexpr std::int64_t last = std::numeric_limits<std::int64_t>::max();
  const auto r = range(core::Time{std::chrono::milliseconds{last - 10}},
                       core::Time{std::chrono::milliseconds{last}});
  const cc::Request request{
      .station = *core::StationId<core::provider::Coops>::make("8729840"),
      .range = r,
      .product = cc::WaterLevel{
          .kind = cc::ObservedLevel{.series = cc::Observed::six_minute},
          .datum = cc::Datum::mllw}};
  CHECK(mov::fetch::coops::initial(request).error() ==
        Error{mov::fetch::Unalignable{.range = r}});
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
  std::optional<Extent<core::Time>> extent;
  if (first and last) {
    extent =
        Extent<core::Time>{.first = core::Time{std::chrono::seconds{*first}},
                           .last = core::Time{std::chrono::seconds{*last}}};
  }
  return {.rows = rows, .extent = extent, .next = std::move(next)};
}

PagingError refusal(const auto& result) { return result.error(); }

}  // namespace

TEST_CASE("a page chain follows next links and ends without one",
          "[fetch][paging]") {
  const Guard g = Guard::first(5);
  CHECK(g.taken() == 1);
  const auto end = g.after(page(3, 0, 10, std::nullopt));
  REQUIRE(end);
  CHECK_FALSE(end->has_value());
  const auto turn = g.after(page(3, 0, 10, "p2"));
  REQUIRE(turn);
  const auto& taken = must(*turn);
  CHECK(taken.link == "p2");
  CHECK(taken.guard.taken() == 2);
  CHECK(g.after(page(0, std::nullopt, std::nullopt, std::nullopt)).has_value());
}

TEST_CASE(
    "a page chain refuses empty pages with a link, cycles, stalls and "
    "runaways",
    "[fetch][paging][regression]") {
  const Guard g = Guard::first(3);
  CHECK(refusal(g.after(page(0, std::nullopt, std::nullopt, "p2"))) ==
        PagingError{.rule = PagingRule::empty_page_with_link, .page = 1});
  const auto second = must(*g.after(page(3, 0, 10, "p2")));
  CHECK(refusal(second.guard.after(page(3, 20, 30, "p2"))) ==
        PagingError{.rule = PagingRule::link_repeated, .page = 2});
  CHECK(refusal(second.guard.after(page(3, 10, 30, "p3"))) ==
        PagingError{.rule = PagingRule::no_progress, .page = 2});
  CHECK(refusal(second.guard.after(page(3, 5, 30, std::nullopt))) ==
        PagingError{.rule = PagingRule::no_progress, .page = 2});
  const auto third = must(*second.guard.after(page(3, 11, 20, "p3")));
  CHECK(third.guard.taken() == 3);
  CHECK(refusal(third.guard.after(page(3, 21, 30, "p4"))) ==
        PagingError{.rule = PagingRule::page_limit, .page = 3});
}

TEST_CASE("a page without positions keeps the last position seen",
          "[fetch][paging][regression]") {
  const Guard g = Guard::first(5);
  const auto second = must(*g.after(page(3, 0, 10, "p2")));
  // Rows, but no extent: the last position (10 s) still stands.
  const auto third =
      must(*second.guard.after(page(3, std::nullopt, std::nullopt, "p3")));
  CHECK(refusal(third.guard.after(page(3, 5, 30, std::nullopt))) ==
        PagingError{.rule = PagingRule::no_progress, .page = 3});
}

TEST_CASE("rows without positions page by count, emptiness and links",
          "[fetch][paging]") {
  using Plain = PageGuard<std::string, NoPosition>;
  const Plain g = Plain::first(2);
  const auto second =
      must(*g.after({.rows = 1, .extent = std::nullopt, .next = "a"}));
  CHECK(refusal(second.guard.after(
            {.rows = 1, .extent = std::nullopt, .next = "b"})) ==
        PagingError{.rule = PagingRule::page_limit, .page = 2});
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

/// A Primary instantaneous gage height with dates and a sublocation.
cu::SeriesInfo epoch(std::string_view id, std::optional<int> first_year,
                     std::optional<int> last_year,
                     std::optional<std::string_view> sublocation = {}) {
  cu::SeriesInfo s = series(id, "00065", "00011", true);
  const auto jan1 = [](std::optional<int> y) {
    return y ? std::optional{std::chrono::sys_days{year{*y} / 1 / 1}}
             : std::nullopt;
  };
  s.validity = *core::ValidRange::make(
      {.first = jan1(first_year), .last = jan1(last_year)});
  if (sublocation) {
    s.sublocation = *cu::Sublocation::make(std::string{*sublocation});
  }
  return s;
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
  REQUIRE(bound->series().epochs().size() == 1);
  CHECK(bound->series().epochs()[0].series().id ==
        *cu::TimeSeriesId::make(id_a));
  CHECK(bound->request() == continuous(r));

  const auto by_id = mov::fetch::usgs::bind(
      daily_by_id(id_c), {series(id_c, "00065", "00003", true)});
  REQUIRE(by_id);
  CHECK(by_id->series().epochs()[0].series().statistic ==
        cu::StatisticId::mean());
}

TEST_CASE("the USGS bind's failures are Resolution failures",
          "[fetch][plan][usgs]") {
  using mov::fetch::Failure;
  using mov::fetch::Resolution;
  const auto r = range(utc(2026, 1, 1), utc(2026, 2, 1));
  // Two sublocations and no group without one.
  const auto ambiguous = mov::fetch::usgs::bind(
      continuous(r), {epoch(id_b, std::nullopt, std::nullopt, "Tailwater"),
                      epoch(id_a, std::nullopt, std::nullopt, "Headwater")});
  CHECK(
      ambiguous.error() ==
      Failure{
          .error =
              mov::fetch::AmbiguousSeries{
                  .candidates = {cu::key_of(epoch(id_a, std::nullopt,
                                                  std::nullopt, "Headwater")),
                                 cu::key_of(epoch(id_b, std::nullopt,
                                                  std::nullopt, "Tailwater"))}},
          .unit = Resolution{.page = 0}});
  // Two Primary epochs of one key are stitched, not ambiguous.
  CHECK(mov::fetch::usgs::bind(continuous(r),
                               {series(id_b, "00065", "00011", true),
                                series(id_a, "00065", "00011", true)})
            .has_value());
  CHECK(mov::fetch::usgs::bind(continuous(r),
                               {series(id_a, "00065", "00011", false)})
            .error()
            .error == Error{mov::fetch::NoData{}});
  // Epochs that end before the range: no data.
  CHECK(mov::fetch::usgs::bind(continuous(r), {epoch(id_a, 1990, 2000)})
            .error()
            .error == Error{mov::fetch::NoData{}});
  CHECK(mov::fetch::usgs::bind(daily_by_id(id_a),
                               {series(id_a, "00065", "00011", true)})
            .error()
            .error ==
        Error{mov::fetch::WrongCollection{.requested = cu::Collection::daily}});
  CHECK(mov::fetch::usgs::bind(continuous(r),
                               {series(id_a, "00065", "00011", true),
                                series(id_a, "00065", "00011", false)})
            .error()
            .error == Error{mov::fetch::DuplicateSeries{
                          .id = *cu::TimeSeriesId::make(id_a)}});
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
  CHECK((*units)[2].epoch == 0);
  CHECK(mu::epoch_of(*bound, (*units)[1]).series().id ==
        *cu::TimeSeriesId::make(id_a));
  CHECK(mu::label((*units)[1]) ==
        UnitLabel{mov::fetch::Page{.window = (*units)[1].window, .index = 0}});
  const auto next = mu::next(
      (*units)[0], PageSummary<Link, core::Time>{
                       .rows = 2,
                       .extent = Extent<core::Time>{.first = utc(2020, 1, 1),
                                                    .last = utc(2020, 2, 1)},
                       .next = "p2"});
  REQUIRE(next);
  REQUIRE(next->size() == 1);
  CHECK((*next)[0].page.link == "p2");
  CHECK((*next)[0].window == (*units)[0].window);
  CHECK((*next)[0].epoch == (*units)[0].epoch);
  CHECK(mu::label((*next)[0]) ==
        UnitLabel{mov::fetch::Page{.window = (*units)[0].window, .index = 1}});
  const auto refused = mu::next(
      (*units)[0], PageSummary<Link, core::Time>{
                       .rows = 0, .extent = std::nullopt, .next = "p2"});
  REQUIRE_FALSE(refused);
  CHECK(
      refused.error() ==
      Error{PagingError{.rule = PagingRule::empty_page_with_link, .page = 1}});
  CHECK(mu::next((*units)[0],
                 PageSummary<Link, core::Time>{
                     .rows = 1, .extent = std::nullopt, .next = std::nullopt})
            ->empty());
}

TEST_CASE("USGS pages must stay in the window they were asked for",
          "[fetch][plan][usgs]") {
  namespace mu = mov::fetch::usgs;
  const auto r = range(utc(2026, 1, 1), utc(2026, 2, 1));
  const auto bound =
      mu::bind(continuous(r), {series(id_a, "00065", "00011", true)});
  REQUIRE(bound);
  const auto units = mu::initial<Link>(*bound);
  REQUIRE(units);
  REQUIRE(units->size() == 1);
  const auto& unit = (*units)[0];
  const auto page_over = [&](core::Time first, core::Time last) {
    return mu::next(
        unit, PageSummary<Link, core::Time>{
                  .rows = 2,
                  .extent = Extent<core::Time>{.first = first, .last = last},
                  .next = std::nullopt});
  };
  const Error outside{
      PagingError{.rule = PagingRule::outside_window, .page = 1}};
  // The first and the last instant of the window are in it.
  CHECK(
      page_over(utc(2026, 1, 1), utc(2026, 2, 1) - std::chrono::milliseconds{1})
          .has_value());
  // A server that ignored datetime: rows before or after the window.
  CHECK(page_over(utc(2025, 12, 31), utc(2026, 1, 2)).error() == outside);
  CHECK(page_over(utc(2026, 1, 30), utc(2026, 2, 1)).error() == outside);
}

TEST_CASE("USGS epochs are planned latest first, each in its own windows",
          "[fetch][plan][usgs]") {
  namespace mu = mov::fetch::usgs;
  // An epoch to 2019 (with a day's margin), one from 2018 on, overlapping;
  // the merge folds in plan order with a left-biased union, so the
  // later-begun epoch's rows win where they overlap.
  const auto r = range(utc(2017, 1, 1), utc(2021, 1, 1));
  const auto bound = mu::bind(continuous(r), {epoch(id_a, 2010, 2019),
                                              epoch(id_b, 2018, std::nullopt)});
  REQUIRE(bound);
  const auto units = mu::initial<Link>(*bound);
  REQUIRE(units);
  // b: 2017-12-31 .. 2021-01-01 (1097 days: two windows); a: 2017-01-01 ..
  // 2019-01-03 (732 days: one window).
  REQUIRE(units->size() == 3);
  CHECK((*units)[0].epoch == 0);
  CHECK((*units)[1].epoch == 0);
  CHECK((*units)[2].epoch == 1);
  CHECK(mu::epoch_of(*bound, (*units)[0]).series().id ==
        *cu::TimeSeriesId::make(id_b));
  CHECK(mu::epoch_of(*bound, (*units)[2]).series().id ==
        *cu::TimeSeriesId::make(id_a));
  CHECK((*units)[0].window.begin() == utc(2017, 12, 31));
  CHECK((*units)[1].window.end() == utc(2021, 1, 1));
  CHECK((*units)[2].window.begin() == utc(2017, 1, 1));
  CHECK((*units)[2].window.end() == utc(2019, 1, 3));
}

TEST_CASE(
    "USGS daily values are one unit per epoch; resolution pages are "
    "bounded",
    "[fetch][plan][usgs]") {
  namespace mu = mov::fetch::usgs;
  const auto bound =
      mu::bind(daily_by_id(id_c), {series(id_c, "00065", "00003", true)});
  REQUIRE(bound);
  const auto units = mu::initial<Link>(*bound);
  REQUIRE(units.size() == 1);
  CHECK(units[0].epoch == 0);
  CHECK(units[0].days == bound->request().days);
  CHECK(mu::label(units[0]) ==
        UnitLabel{mov::fetch::DayWindow{.days = units[0].days}});
  const auto day = [](unsigned d) {
    return std::chrono::local_days{year{2026} / std::chrono::January /
                                   std::chrono::day{d}};
  };
  const auto next = mu::next(
      units[0], PageSummary<Link, std::chrono::local_days>{
                    .rows = 3,
                    .extent = Extent<std::chrono::local_days>{.first = day(1),
                                                              .last = day(3)},
                    .next = "d2"});
  REQUIRE(next);
  CHECK((*next)[0].days == units[0].days);
  CHECK((*next)[0].epoch == units[0].epoch);

  auto unit = mu::first_resolution_page<Link>();
  CHECK(mu::label(unit) == UnitLabel{mov::fetch::Resolution{.page = 0}});
  for (std::uint32_t taken = 1; taken < mu::max_resolution_pages; ++taken) {
    const auto more = mu::next(unit, PageSummary<Link, NoPosition>{
                                         .rows = 1,
                                         .extent = std::nullopt,
                                         .next = "r" + std::to_string(taken)});
    REQUIRE(more);
    REQUIRE(more->size() == 1);
    unit = (*more)[0];
  }
  CHECK(mu::label(unit) == UnitLabel{mov::fetch::Resolution{.page = 3}});
  CHECK_FALSE(mu::next(unit,
                       PageSummary<Link, NoPosition>{
                           .rows = 1, .extent = std::nullopt, .next = "r9"})
                  .has_value());
}
