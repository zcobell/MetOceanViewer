// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <concepts>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/provider/usgs.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/core/windows.hpp"

namespace usgs = mov::core::usgs;

using mov::core::GenericQuantity;
using mov::core::Quantity;
using mov::core::QuantityId;
using usgs::ParameterCode;
using usgs::ResolveError;
using usgs::SeriesInfo;
using usgs::SeriesSet;
using usgs::StatisticId;
using usgs::TimeSeriesId;

namespace {

ParameterCode parameter(std::string_view text) {
  const auto code = ParameterCode::make(text);
  REQUIRE(code.has_value());
  return *code;
}

TimeSeriesId series_id(char fill) {
  const auto id = TimeSeriesId::make(std::string(32, fill));
  REQUIRE(id.has_value());
  return *id;
}

SeriesInfo row(char id, std::string_view code, StatisticId stat,
               bool primary = true) {
  return {.id = series_id(id),
          .parameter = parameter(code),
          .statistic = stat,
          .parameter_name = "name of " + std::string{code},
          .primary = primary,
          .unit = std::nullopt,
          .validity = mov::core::ValidRange{}};
}

SeriesSet set_of(std::vector<SeriesInfo> rows) {
  auto set = SeriesSet::make(std::move(rows));
  REQUIRE(set.has_value());
  return std::move(set).value_or(SeriesSet{});
}

/// A river site: Primary instantaneous discharge and gage height, a second
/// (not Primary) discharge series, and the daily mean of discharge.
SeriesSet river() {
  return set_of({row('b', "00065", StatisticId::instantaneous()),
                 row('a', "00060", StatisticId::instantaneous()),
                 row('d', "00060", StatisticId::instantaneous(), false),
                 row('c', "00060", StatisticId::mean())});
}

mov::core::StationId<mov::core::provider::Usgs> site_id() {
  auto id =
      mov::core::StationId<mov::core::provider::Usgs>::make("USGS-07374000");
  REQUIRE(id.has_value());
  return *std::move(id);
}

usgs::Continuous continuous(
    std::variant<usgs::ByParameter, usgs::ByTimeSeries> selector) {
  const auto range =
      mov::core::TimeRange::make(mov::core::Time{std::chrono::milliseconds{0}},
                                 mov::core::Time{std::chrono::milliseconds{1}});
  REQUIRE(range.has_value());
  return {.station = site_id(), .series = std::move(selector), .range = *range};
}

usgs::Daily daily(
    std::variant<usgs::DailyByParameter, usgs::ByTimeSeries> selector) {
  using std::chrono::days;
  using std::chrono::local_days;
  const auto range =
      mov::core::DayRange::make(local_days{days{0}}, local_days{days{13'150}});
  REQUIRE(range.has_value());
  return {.station = site_id(), .series = std::move(selector), .days = *range};
}

usgs::ByParameter by_parameter(std::string_view code) {
  return {.parameter = parameter(code)};
}

usgs::DailyByParameter daily_by(std::string_view code, StatisticId stat) {
  return {.parameter = parameter(code), .statistic = stat};
}

usgs::ByTimeSeries by_id(char id) { return {.id = series_id(id)}; }

usgs::Sublocation sub(std::string_view text) {
  auto s = usgs::Sublocation::make(std::string{text});
  REQUIRE(s.has_value());
  return *std::move(s);
}

std::chrono::sys_days day(int y, unsigned m, unsigned d) {
  return std::chrono::sys_days{std::chrono::year{y} / std::chrono::month{m} /
                               std::chrono::day{d}};
}

std::chrono::local_days local(int y, unsigned m, unsigned d) {
  return std::chrono::local_days{day(y, m, d).time_since_epoch()};
}

mov::core::Time utc(int y, unsigned m, unsigned d) {
  return std::chrono::time_point_cast<std::chrono::milliseconds>(day(y, m, d));
}

mov::core::TimeRange time_range(mov::core::Time b, mov::core::Time e) {
  const auto r = mov::core::TimeRange::make(b, e);
  REQUIRE(r.has_value());
  return *r;
}

mov::core::DayRange day_range(std::chrono::local_days first,
                              std::chrono::local_days end) {
  const auto r = mov::core::DayRange::make(first, end);
  REQUIRE(r.has_value());
  return *r;
}

/// A Primary series with dates.
SeriesInfo dated(char id, std::string_view code,
                 std::optional<std::chrono::sys_days> first,
                 std::optional<std::chrono::sys_days> last,
                 StatisticId stat = StatisticId::instantaneous()) {
  SeriesInfo s = row(id, code, stat);
  const auto validity =
      mov::core::ValidRange::make({.first = first, .last = last});
  REQUIRE(validity.has_value());
  s.validity = *validity;
  return s;
}

SeriesInfo at_sublocation(SeriesInfo s, std::string_view where) {
  s.sublocation = sub(where);
  return s;
}

/// The id letters of a span of series, in order.
std::string ids(std::span<const SeriesInfo> series) {
  std::string out;
  for (const SeriesInfo& s : series) {
    out += s.id.view().front();
  }
  return out;
}

usgs::Continuous continuous_over(
    std::variant<usgs::ByParameter, usgs::ByTimeSeries> selector,
    mov::core::TimeRange range) {
  return {.station = site_id(), .series = std::move(selector), .range = range};
}

usgs::Daily daily_over(
    std::variant<usgs::DailyByParameter, usgs::ByTimeSeries> selector,
    mov::core::DayRange days) {
  return {.station = site_id(), .series = std::move(selector), .days = days};
}

template <class T>
concept HasEpochs = requires(T&& t) { std::forward<T>(t).epochs(); };

}  // namespace

TEST_CASE("USGS records are values", "[core][usgs]") {
  STATIC_REQUIRE(std::copyable<SeriesInfo>);
  STATIC_REQUIRE(std::equality_comparable<SeriesInfo>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<SeriesInfo>);
  STATIC_REQUIRE(std::regular<SeriesSet>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<SeriesSet>);
  STATIC_REQUIRE(std::totally_ordered<usgs::SeriesKey>);
  STATIC_REQUIRE(std::totally_ordered<usgs::Sublocation>);
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<usgs::Sublocation>);
  STATIC_REQUIRE(std::copyable<usgs::Request>);
  STATIC_REQUIRE(std::equality_comparable<usgs::Request>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<usgs::Request>);
  STATIC_REQUIRE(std::copyable<usgs::Resolved<usgs::Continuous>>);
  STATIC_REQUIRE(std::equality_comparable<usgs::Resolved<usgs::Daily>>);
  // A resolved series and its epochs come only from resolve() (core's key),
  // and the collections do not mix.
  STATIC_REQUIRE_FALSE(
      std::is_constructible_v<usgs::Resolved<usgs::Continuous>, usgs::SeriesKey,
                              std::vector<usgs::Epoch<usgs::Continuous>>>);
  STATIC_REQUIRE_FALSE(
      std::is_constructible_v<usgs::Epoch<usgs::Continuous>, SeriesInfo,
                              mov::core::TimeRange>);
  STATIC_REQUIRE_FALSE(std::is_convertible_v<usgs::Resolved<usgs::Daily>,
                                             usgs::Resolved<usgs::Continuous>>);
  STATIC_REQUIRE(
      std::same_as<usgs::Epoch<usgs::Daily>::Range, mov::core::DayRange>);
  STATIC_REQUIRE(HasEpochs<const usgs::Resolved<usgs::Daily>&>);
  STATIC_REQUIRE_FALSE(HasEpochs<usgs::Resolved<usgs::Daily>>);
  // A continuous selector has no statistic to name.
  STATIC_REQUIRE_FALSE(std::is_constructible_v<
                       std::variant<usgs::ByParameter, usgs::ByTimeSeries>,
                       usgs::DailyByParameter>);
}

TEST_CASE("a sublocation is USGS's text, unaltered", "[core][usgs]") {
  CHECK(usgs::Sublocation::make("Headwater")
            .transform([](const usgs::Sublocation& s) {
              return std::string{s.view()};
            }) == std::string{"Headwater"});
  CHECK(usgs::Sublocation::make("Lock Chamber").has_value());
  CHECK(usgs::Sublocation::make("Station \xC3\xA0 l'eau").has_value());
  CHECK(usgs::Sublocation::make(std::string(128, 'x')).has_value());
  CHECK(usgs::Sublocation::make("") ==
        std::unexpected{usgs::SublocationError::empty});
  CHECK(usgs::Sublocation::make(std::string(129, 'x')) ==
        std::unexpected{usgs::SublocationError::too_long});
  for (const std::string_view bad :
       {" Headwater", "Headwater ", "Head\twater", "a\x7F", "\xC3"}) {
    CHECK(usgs::Sublocation::make(std::string{bad}) ==
          std::unexpected{usgs::SublocationError::invalid_text});
  }
  CHECK(usgs::Sublocation::make(std::string{"a\0b", 3}) ==
        std::unexpected{usgs::SublocationError::invalid_text});
  CHECK(sub("Headwater") < sub("Tailwater"));
}

// At run time: a usgs::Request holds a std::string inside a variant, which
// libc++ 18 cannot constant-evaluate (core-design 1.2).
TEST_CASE("USGS continuous requests span at most 1000 days", "[core][usgs]") {
  CHECK(usgs::max_window(continuous(by_parameter("00065"))).duration() ==
        std::chrono::days{1000});
  CHECK(usgs::max_continuous_window.duration() == std::chrono::days{1000});
  const usgs::Request c = continuous(by_parameter("00065"));
  const usgs::Request d = daily(daily_by("00060", StatisticId::mean()));
  CHECK(c != d);
  CHECK(c == usgs::Request{continuous(by_parameter("00065"))});
  CHECK(c != usgs::Request{continuous(
                 usgs::ByParameter{.parameter = parameter("00065"),
                                   .sublocation = sub("Headwater")})});
}

TEST_CASE("a series set is sorted and unique by id", "[core][usgs]") {
  const SeriesSet site = river();
  REQUIRE(site.size() == 4);
  CHECK(site.items()[0].id == series_id('a'));
  CHECK(site.items()[3].id == series_id('d'));
  const SeriesInfo* c = site.find(series_id('c'));
  REQUIRE(c != nullptr);
  CHECK(c->statistic == StatisticId::mean());
  CHECK(site.find(series_id('e')) == nullptr);
  CHECK(site.find(series_id('0')) == nullptr);
  CHECK(site.find(series_id('f')) == nullptr);
  CHECK(SeriesSet{}.empty());
  const SeriesSet none;
  CHECK(none.keys().empty());
  CHECK(none.find(series_id('a')) == nullptr);
  // Built in another order, the same set.
  CHECK(set_of({row('c', "00060", StatisticId::mean()),
                row('d', "00060", StatisticId::instantaneous(), false),
                row('a', "00060", StatisticId::instantaneous()),
                row('b', "00065", StatisticId::instantaneous())}) == site);
  // A repeated id is the one refusal.
  CHECK(SeriesSet::make({row('b', "00065", StatisticId::instantaneous()),
                         row('a', "00060", StatisticId::mean()),
                         row('b', "00060", StatisticId::mean(), false)}) ==
        std::unexpected{usgs::DuplicateSeriesId{.id = series_id('b')}});
}

TEST_CASE("a series set groups Primary epochs by key", "[core][usgs]") {
  // Three Primary epochs of one key, overlapping (as at USGS-01646500), one
  // of them with unknown dates; a Primary series at a sublocation; a series
  // that is not Primary.
  const SeriesSet site =
      set_of({dated('c', "00010", day(2019, 7, 24), std::nullopt),
              dated('a', "00010", day(2007, 10, 1), day(2019, 10, 1)),
              dated('e', "00010", std::nullopt, std::nullopt),
              dated('b', "00010", day(2013, 11, 23), day(2019, 5, 27)),
              at_sublocation(dated('d', "00010", day(2023, 1, 2), std::nullopt),
                             "Headwater"),
              row('f', "00010", StatisticId::instantaneous(), false)});
  const usgs::SeriesKey main{.parameter = parameter("00010"),
                             .statistic = StatisticId::instantaneous(),
                             .sublocation = std::nullopt};
  const usgs::SeriesKey headwater{.parameter = parameter("00010"),
                                  .statistic = StatisticId::instantaneous(),
                                  .sublocation = sub("Headwater")};
  CHECK(std::vector<usgs::SeriesKey>(site.keys().begin(), site.keys().end()) ==
        std::vector<usgs::SeriesKey>{main, headwater});
  CHECK(ids(site.epochs(main)) == "eabc");  // unknown begin first
  CHECK(ids(site.epochs(headwater)) == "d");
  CHECK(site.epochs({.parameter = parameter("00060"),
                     .statistic = StatisticId::instantaneous(),
                     .sublocation = std::nullopt})
            .empty());
  CHECK(usgs::key_of(*site.find(series_id('d'))) == headwater);
  // Epochs of one begin day order by id.
  const SeriesSet ties =
      set_of({dated('b', "00060", day(2020, 1, 1), std::nullopt),
              dated('a', "00060", day(2020, 1, 1), day(2021, 1, 1))});
  CHECK(ids(ties.epochs(usgs::key_of(*ties.find(series_id('a'))))) == "ab");
}

TEST_CASE("resolution stitches a key's epochs that meet the range",
          "[core][usgs]") {
  const SeriesSet site =
      set_of({dated('a', "00060", day(2007, 10, 1), day(2019, 10, 1)),
              dated('b', "00060", day(2013, 11, 23), day(2019, 5, 27)),
              dated('c', "00060", day(2019, 7, 24), std::nullopt),
              dated('9', "00060", day(1990, 1, 1), day(1999, 12, 31))});
  const auto range = time_range(utc(2018, 1, 1), utc(2026, 1, 1));
  const auto r =
      usgs::resolve(continuous_over(by_parameter("00060"), range), site);
  REQUIRE(r.has_value());
  CHECK(r->key() == usgs::key_of(*site.find(series_id('a'))));
  // Precedence order: the epoch begun last first. '9' ended long before.
  REQUIRE(r->epochs().size() == 3);
  CHECK(r->epochs()[0].series().id == series_id('c'));
  CHECK(r->epochs()[1].series().id == series_id('b'));
  CHECK(r->epochs()[2].series().id == series_id('a'));
  // Each epoch's range: the request within its validity, a day wider at a
  // known end; an ongoing epoch runs to the request's end.
  CHECK((r->epochs()[0].range() ==
         time_range(utc(2019, 7, 23), utc(2026, 1, 1))));
  CHECK((r->epochs()[1].range() ==
         time_range(utc(2018, 1, 1), utc(2019, 5, 29))));
  CHECK((r->epochs()[2].range() ==
         time_range(utc(2018, 1, 1), utc(2019, 10, 3))));
  // A range no epoch meets: no series.
  CHECK(usgs::resolve(
            continuous_over(by_parameter("00060"),
                            time_range(utc(2001, 1, 1), utc(2002, 1, 1))),
            site) == std::unexpected{ResolveError{usgs::NoSeries{}}});
  // An epoch of unknown dates meets every range.
  const SeriesSet unknown =
      set_of({row('6', "00060", StatisticId::instantaneous())});
  const auto any =
      usgs::resolve(continuous_over(by_parameter("00060"), range), unknown);
  REQUIRE(any.has_value());
  CHECK((any->epochs()[0].range() == range));
}

TEST_CASE("daily resolution gives each epoch its days", "[core][usgs]") {
  const SeriesSet site = set_of({dated('a', "00060", day(2000, 1, 1),
                                       day(2010, 6, 30), StatisticId::mean()),
                                 dated('b', "00060", day(2010, 7, 1),
                                       std::nullopt, StatisticId::mean())});
  const auto days = day_range(local(2009, 1, 1), local(2012, 1, 1));
  const auto r = usgs::resolve(
      daily_over(daily_by("00060", StatisticId::mean()), days), site);
  REQUIRE(r.has_value());
  REQUIRE(r->epochs().size() == 2);
  CHECK(r->epochs()[0].series().id == series_id('b'));
  CHECK((r->epochs()[0].range() ==
         day_range(local(2010, 6, 30), local(2012, 1, 1))));
  CHECK((r->epochs()[1].range() ==
         day_range(local(2009, 1, 1), local(2010, 7, 2))));
}

TEST_CASE("resolution chooses the sublocation group", "[core][usgs]") {
  // USGS-02431011: gage heights at Headwater, Tailwater and Lock Chamber.
  const SeriesSet lock =
      set_of({at_sublocation(row('a', "00065", StatisticId::instantaneous()),
                             "Headwater"),
              at_sublocation(row('b', "00065", StatisticId::instantaneous()),
                             "Tailwater"),
              at_sublocation(row('c', "00065", StatisticId::instantaneous()),
                             "Lock Chamber"),
              row('d', "00060", StatisticId::instantaneous())});
  const auto ambiguous = usgs::resolve(continuous(by_parameter("00065")), lock);
  REQUIRE_FALSE(ambiguous.has_value());
  const auto* candidates =
      std::get_if<usgs::AmbiguousSeries>(&ambiguous.error());
  REQUIRE(candidates != nullptr);
  REQUIRE(candidates->candidates.size() == 3);
  CHECK(candidates->candidates[0].sublocation == sub("Headwater"));
  CHECK(candidates->candidates[1].sublocation == sub("Lock Chamber"));
  CHECK(candidates->candidates[2].sublocation == sub("Tailwater"));
  // Named, it resolves; a name the site does not have is no series.
  const auto tail = usgs::resolve(
      continuous(usgs::ByParameter{.parameter = parameter("00065"),
                                   .sublocation = sub("Tailwater")}),
      lock);
  REQUIRE(tail.has_value());
  CHECK(tail->epochs()[0].series().id == series_id('b'));
  CHECK(usgs::resolve(
            continuous(usgs::ByParameter{.parameter = parameter("00065"),
                                         .sublocation = sub("Spillway")}),
            lock) == std::unexpected{ResolveError{usgs::NoSeries{}}});
  // One group at a sublocation is the only choice.
  CHECK(usgs::resolve(
            continuous(by_parameter("00065")),
            set_of({at_sublocation(
                row('a', "00065", StatisticId::instantaneous()), "Headwater")}))
            .has_value());
  // A group without a sublocation is the one meant when none is named.
  const SeriesSet mixed =
      set_of({row('7', "00065", StatisticId::instantaneous()),
              at_sublocation(row('8', "00065", StatisticId::instantaneous()),
                             "Headwater")});
  const auto main = usgs::resolve(continuous(by_parameter("00065")), mixed);
  REQUIRE(main.has_value());
  CHECK(main->epochs()[0].series().id == series_id('7'));
  // Daily, likewise.
  CHECK(std::holds_alternative<usgs::AmbiguousSeries>(
      usgs::resolve(
          daily(daily_by("00065", StatisticId::mean())),
          set_of({at_sublocation(row('a', "00065", StatisticId::mean()),
                                 "Headwater"),
                  at_sublocation(row('b', "00065", StatisticId::mean()),
                                 "Tailwater")}))
          .error()));
}

TEST_CASE("resolution by parameter needs a Primary epoch", "[core][usgs]") {
  const auto c = usgs::resolve(continuous(by_parameter("00060")), river());
  REQUIRE(c.has_value());
  CHECK(c->epochs().size() == 1);
  CHECK(c->epochs()[0].series().id == series_id('a'));
  const auto d =
      usgs::resolve(daily(daily_by("00060", StatisticId::mean())), river());
  REQUIRE(d.has_value());
  CHECK(d->epochs()[0].series().parameter_name == "name of 00060");
  CHECK(usgs::resolve(continuous(by_parameter("62620")), river()) ==
        std::unexpected{ResolveError{usgs::NoSeries{}}});
  CHECK(usgs::resolve(daily(daily_by("00065", StatisticId::mean())), river()) ==
        std::unexpected{ResolveError{usgs::NoSeries{}}});
  const SeriesSet only_secondary =
      set_of({row('d', "00060", StatisticId::instantaneous(), false)});
  CHECK(usgs::resolve(continuous(by_parameter("00060")), only_secondary) ==
        std::unexpected{ResolveError{usgs::NoSeries{}}});
  // A daily request naming the instantaneous statistic.
  CHECK(usgs::resolve(daily(daily_by("00060", StatisticId::instantaneous())),
                      river()) ==
        std::unexpected{ResolveError{usgs::WrongCollection{}}});
}

TEST_CASE("resolution by id takes that epoch over the whole range",
          "[core][usgs]") {
  // Not Primary, but named: resolution takes it.
  const auto secondary = usgs::resolve(continuous(by_id('d')), river());
  REQUIRE(secondary.has_value());
  REQUIRE(secondary->epochs().size() == 1);
  CHECK_FALSE(secondary->epochs()[0].series().primary);
  CHECK((secondary->epochs()[0].range() == continuous(by_id('d')).range));
  const SeriesSet site = river();
  CHECK(secondary->key() == usgs::key_of(*site.find(series_id('d'))));
  // Its validity does not narrow a series named by id.
  const SeriesSet ended =
      set_of({dated('a', "00060", day(1990, 1, 1), day(1991, 1, 1))});
  const auto range = time_range(utc(2020, 1, 1), utc(2021, 1, 1));
  const auto named = usgs::resolve(continuous_over(by_id('a'), range), ended);
  REQUIRE(named.has_value());
  CHECK((named->epochs()[0].range() == range));
  CHECK(usgs::resolve(daily(by_id('c')), river()).has_value());
  CHECK(usgs::resolve(continuous(by_id('c')), river()) ==
        std::unexpected{ResolveError{usgs::WrongCollection{}}});
  CHECK(usgs::resolve(daily(by_id('a')), river()) ==
        std::unexpected{ResolveError{usgs::WrongCollection{}}});
  CHECK(usgs::resolve(continuous(by_id('f')), river()) ==
        std::unexpected{ResolveError{usgs::NoSeries{}}});
  CHECK(usgs::resolve(daily(by_id('f')), river()) ==
        std::unexpected{ResolveError{usgs::NoSeries{}}});
  // Two resolutions of one series are equal.
  CHECK(usgs::resolve(continuous(by_id('a')), river()) ==
        usgs::resolve(continuous(by_parameter("00060")), river()));
}

TEST_CASE("the map shows the group whose data reach latest", "[core][usgs]") {
  const auto instantaneous = StatisticId::instantaneous();
  // Headwater ended in 2025, Tailwater is ongoing, the lock chamber has no
  // dates at all.
  const SeriesSet lock = set_of(
      {at_sublocation(dated('a', "00065", day(2023, 1, 2), day(2025, 4, 4)),
                      "Headwater"),
       at_sublocation(dated('b', "00065", day(2023, 1, 2), std::nullopt),
                      "Tailwater"),
       at_sublocation(row('c', "00065", instantaneous), "Lock Chamber")});
  CHECK(usgs::map_default(lock, parameter("00065"), instantaneous) ==
        usgs::key_of(*lock.find(series_id('b'))));
  // Both ended: the later end; then the later begin.
  const SeriesSet ended = set_of(
      {at_sublocation(dated('a', "00065", day(2001, 1, 1), day(2020, 1, 1)),
                      "Headwater"),
       at_sublocation(dated('b', "00065", day(2000, 1, 1), day(2019, 1, 1)),
                      "Tailwater"),
       at_sublocation(dated('c', "00065", day(2002, 1, 1), day(2019, 1, 1)),
                      "Lock")});
  CHECK(usgs::map_default(ended, parameter("00065"), instantaneous) ==
        usgs::key_of(*ended.find(series_id('a'))));
  const SeriesSet tie = set_of(
      {at_sublocation(dated('b', "00065", day(2000, 1, 1), day(2019, 1, 1)),
                      "Tailwater"),
       at_sublocation(dated('c', "00065", day(2002, 1, 1), day(2019, 1, 1)),
                      "Lock")});
  CHECK(usgs::map_default(tie, parameter("00065"), instantaneous) ==
        usgs::key_of(*tie.find(series_id('c'))));
  CHECK_FALSE(
      usgs::map_default(lock, parameter("00060"), instantaneous).has_value());
  CHECK_FALSE(usgs::map_default(SeriesSet{}, parameter("00065"), instantaneous)
                  .has_value());
}

TEST_CASE("USGS parameters name their quantity", "[core][usgs]") {
  CHECK(usgs::quantity_of(parameter("00060")) ==
        QuantityId{Quantity::discharge});
  CHECK(usgs::quantity_of(parameter("62619")) ==
        QuantityId{Quantity::water_level});
  CHECK(usgs::quantity_of(parameter("00010")) ==
        QuantityId{Quantity::water_temperature});
  // Outside the registry: the generic quantity usgs_<code>, the same value
  // GenericQuantity::parse makes of that token.
  const QuantityId precipitation = usgs::quantity_of(parameter("00045"));
  CHECK(mov::core::token(precipitation) == "usgs_00045");
  const auto parsed =
      GenericQuantity::parse({.token = "usgs_00045", .standard_name = ""});
  REQUIRE(parsed.has_value());
  CHECK(precipitation == QuantityId{parsed.value_or(GenericQuantity{})});
}

TEST_CASE("every registry datum suits its quantity", "[core][usgs]") {
  for (const std::string_view code : {"00010", "00020", "00035", "00036",
                                      "00060", "00065", "62620", "62619"}) {
    const auto mapping = usgs::registry_quantity(parameter(code));
    REQUIRE(mapping.has_value());
    if (mapping and mapping->datum) {
      CHECK(mov::core::datum_applicable(QuantityId{mapping->quantity}));
    }
  }
}
