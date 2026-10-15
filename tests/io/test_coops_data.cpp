// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// coops::read_data and its parse: the recorded datagetter bodies (goldens)
// and hostile ones (docs/providers-design.md sections 4.3 and 10.1).

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/provider/coops.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/stop_token.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"
#include "mov/io/detail/coops_json.hpp"
#include "mov/io/error.hpp"
#include "mov/io/part.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/coops.hpp"
#include "wire_test_support.hpp"

namespace {

namespace cc = mov::core::coops;
namespace wc = mov::io::wire::coops;
using mov::core::Quantity;
using mov::core::Time;
using mov::core::TimeSeries;
using mov::core::VerticalDatum;
using mov::io::Error;
using mov::io::ParseErrc;
using mov::io::Read;
using mov::io::ReadContext;
using mov::io::Warning;
using mov::io::WarningCode;
using mov::test::coops_fixture;
using mov::test::parse_error_in;
using mov::test::utc;
using mov::test::values_of;
using Opt = std::optional<double>;

using Result = std::expected<Read<mov::io::Part>, Error>;

wc::Series observed(cc::Observed s = cc::Observed::six_minute,
                    cc::Datum d = cc::Datum::msl) {
  return wc::Observation{.series = s, .datum = d};
}
wc::Series predicted(cc::PredictionInterval i = cc::PredictionInterval::six,
                     cc::Datum d = cc::Datum::mllw) {
  return wc::Prediction{.interval = i, .datum = d};
}
wc::Series met(cc::Met m, cc::MetInterval i = cc::MetInterval::six_minute) {
  return cc::Meteorological{.product = m, .interval = i};
}

Result parse(std::string_view body, const wc::Series& series,
             const ReadContext& ctx = {}) {
  return mov::io::detail::coops::parse_data(body, series, ctx);
}

Read<mov::io::Part> parsed(std::string_view body, const wc::Series& series) {
  auto r = parse(body, series);
  if (not r) {
    const auto* p = std::get_if<mov::io::ParseError>(&r.error());
    INFO((p != nullptr ? p->context() : std::string{"not a ParseError"}));
    REQUIRE(r.has_value());
  }
  return *std::move(r);
}

const TimeSeries& column(const Read<mov::io::Part>& r, std::string_view token) {
  const auto& columns = r.value.columns();
  const auto it = std::ranges::find_if(columns, [token](const auto& entry) {
    return mov::io::format(entry.first) == token;
  });
  REQUIRE(it != columns.end());
  return it->second;
}

std::vector<Time> six_minute_times(std::size_t n) {
  std::vector<Time> out;
  out.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    out.push_back(utc(2026, 10, 1, 0, static_cast<unsigned>(6 * i)));
  }
  return out;
}

std::vector<Time> times_of(const TimeSeries& s) {
  return {s.times().begin(), s.times().end()};
}

mov::core::Unit unit(std::string_view text) {
  const auto u = mov::core::parse_unit(text);
  REQUIRE(u.has_value());
  return u.value_or(mov::core::Unit{});
}

// A data body of the given records.
std::string data_body(std::string_view records) {
  return R"({"metadata":{"id":"8729840"}, "data": [)" + std::string{records} +
         "]}";
}

ParseErrc code_of(const Result& r) { return parse_error_in(r).code(); }

}  // namespace

// ---- goldens
// -------------------------------------------------------------------

TEST_CASE("a 6-minute water level: values, meta, flags and quality",
          "[io][wire][coops]") {
  const auto r = parsed(coops_fixture("water_level.json"), observed());
  REQUIRE(r.value.columns().size() == 1);
  const TimeSeries& wl = column(r, "water_level");
  CHECK(times_of(wl) == six_minute_times(6));
  CHECK(values_of(wl) ==
        std::vector<Opt>{0.409, 0.412, 0.422, 0.429, 0.436, 0.442});
  CHECK(wl.meta().quantity() == mov::core::QuantityId{Quantity::water_level});
  CHECK(wl.meta().label() == "Water level");
  CHECK(wl.meta().unit() == unit("m"));
  CHECK(wl.meta().datum() == VerticalDatum::msl);
  // Two records have a nonzero flag; all six are preliminary.
  CHECK(r.warnings ==
        std::vector<Warning>{{.code = WarningCode::suspect_samples_kept,
                              .subject = "coops:f",
                              .count = 2},
                             {.code = WarningCode::provisional_samples,
                              .subject = "coops:q",
                              .count = 6}});
}

TEST_CASE("the datum asked for is the series' datum", "[io][wire][coops]") {
  const auto mllw = parsed(coops_fixture("water_level_mllw.json"),
                           observed(cc::Observed::six_minute, cc::Datum::mllw));
  CHECK(column(mllw, "water_level").meta().datum() == VerticalDatum::mllw);
  CHECK(values_of(column(mllw, "water_level")).front() == Opt{0.597});
  const auto igld =
      parsed(coops_fixture("water_level_igld.json"),
             observed(cc::Observed::six_minute, cc::Datum::igld85));
  CHECK(column(igld, "water_level").meta().datum() == VerticalDatum::igld85);
  CHECK(values_of(column(igld, "water_level")) ==
        std::vector<Opt>{174.222, 174.227, 174.236});
}

TEST_CASE("hourly and one-minute water levels", "[io][wire][coops]") {
  const auto hourly = parsed(coops_fixture("hourly_height.json"),
                             observed(cc::Observed::hourly, cc::Datum::mllw));
  const TimeSeries& h = column(hourly, "water_level");
  CHECK(times_of(h) == std::vector<Time>{utc(2025, 1, 1, 0), utc(2025, 1, 1, 1),
                                         utc(2025, 1, 1, 2),
                                         utc(2025, 1, 1, 3)});
  CHECK(values_of(h) == std::vector<Opt>{0.425, 0.492, 0.548, 0.585});
  CHECK(hourly.warnings.empty());  // flags "0,0", no q

  const auto minute = parsed(coops_fixture("one_minute_water_level.json"),
                             observed(cc::Observed::one_minute));
  const TimeSeries& m = column(minute, "water_level");
  CHECK(m.size() == 13);
  CHECK(m.times()[12] == utc(2026, 10, 1, 0, 12));
  CHECK(values_of(m)[12] == Opt{0.421});
  CHECK(minute.warnings.empty());
}

TEST_CASE("predictions: their own envelope, quantity and datum",
          "[io][wire][coops]") {
  const auto six = parsed(coops_fixture("predictions.json"), predicted());
  const TimeSeries& p = column(six, "water_level_prediction");
  CHECK(times_of(p) == six_minute_times(3));
  CHECK(values_of(p) == std::vector<Opt>{0.11, 0.116, 0.122});
  CHECK(p.meta().quantity() ==
        mov::core::QuantityId{Quantity::water_level_prediction});
  CHECK(p.meta().label() == "Predicted water level");
  CHECK(p.meta().datum() == VerticalDatum::mllw);
  CHECK(six.warnings.empty());

  const auto navd =
      parsed(coops_fixture("predictions_six_minute_navd.json"),
             predicted(cc::PredictionInterval::six, cc::Datum::navd88));
  CHECK(column(navd, "water_level_prediction").meta().datum() ==
        VerticalDatum::navd88);
  const auto hourly = parsed(coops_fixture("predictions_hourly.json"),
                             predicted(cc::PredictionInterval::sixty));
  CHECK(times_of(column(hourly, "water_level_prediction")) ==
        std::vector<Time>{utc(2026, 10, 1, 0), utc(2026, 10, 1, 1),
                          utc(2026, 10, 1, 2)});
}

TEST_CASE("wind is three columns, not a split string",
          "[io][wire][coops][regression][L5]") {
  const auto r = parsed(coops_fixture("wind.json"), met(cc::Met::wind));
  REQUIRE(r.value.columns().size() == 3);
  const TimeSeries& speed = column(r, "wind_speed");
  const TimeSeries& direction = column(r, "wind_direction");
  const TimeSeries& gust = column(r, "wind_gust");
  CHECK(values_of(speed) == std::vector<Opt>{3.6, 3.5, 3.2, 2.8, 3.2, 0.9});
  CHECK(values_of(direction) ==
        std::vector<Opt>{157.0, 156.0, 149.0, 146.0, 151.0, 141.0});
  CHECK(values_of(gust) == std::vector<Opt>{5.8, 5.3, 5.1, 4.2, 5.4, 4.7});
  CHECK(times_of(speed) == six_minute_times(6));
  CHECK(times_of(gust) == times_of(direction));
  CHECK(speed.meta().unit() == unit("m/s"));
  CHECK(direction.meta().unit() == unit("degree"));
  CHECK(not speed.meta().datum().has_value());
  CHECK(r.warnings.empty());
  const auto hourly = parsed(coops_fixture("wind_hourly.json"),
                             met(cc::Met::wind, cc::MetInterval::hourly));
  CHECK(values_of(column(hourly, "wind_gust")) ==
        std::vector<Opt>{5.8, 2.1, 4.8});
}

TEST_CASE("met products: quantity and metric unit", "[io][wire][coops]") {
  struct Row {
    std::string_view fixture;
    cc::Met product;
    Quantity quantity;
    std::string_view unit_text;
    std::vector<Opt> values;
  };
  const std::vector<Row> rows{
      {.fixture = "air_temperature.json",
       .product = cc::Met::air_temperature,
       .quantity = Quantity::air_temperature,
       .unit_text = "degC",
       .values = {28.5, 28.3, 28.4}},
      {.fixture = "water_temperature.json",
       .product = cc::Met::water_temperature,
       .quantity = Quantity::water_temperature,
       .unit_text = "degC",
       .values = {29.1, 29.1, 29.1}},
      {.fixture = "air_pressure.json",
       .product = cc::Met::air_pressure,
       .quantity = Quantity::air_pressure,
       .unit_text = "hPa",
       .values = {1014.7, 1014.8, 1015.0}},
      {.fixture = "humidity.json",
       .product = cc::Met::humidity,
       .quantity = Quantity::relative_humidity,
       .unit_text = "percent",
       .values = {100.0, 100.0, 100.0}},
      {.fixture = "visibility.json",
       .product = cc::Met::visibility,
       .quantity = Quantity::visibility,
       .unit_text = "km",
       .values = {10.0, 10.0, 10.0}},
  };
  for (const Row& row : rows) {
    INFO(row.fixture);
    const auto r = parsed(coops_fixture(row.fixture), met(row.product));
    REQUIRE(r.value.columns().size() == 1);
    const TimeSeries& s = r.value.columns().begin()->second;
    CHECK(s.meta().quantity() == mov::core::QuantityId{row.quantity});
    CHECK(s.meta().unit() == unit(row.unit_text));
    CHECK(values_of(s) == row.values);
    CHECK(times_of(s) == six_minute_times(3));
    CHECK(r.warnings.empty());
  }
}

TEST_CASE("the extra products: conductivity, salinity, air gap",
          "[io][wire][coops]") {
  const auto conductivity =
      parsed(coops_fixture("conductivity.json"), met(cc::Met::conductivity));
  const TimeSeries& c = column(conductivity, "conductivity");
  CHECK(values_of(c) == std::vector<Opt>{36.42, 36.42, 36.41});
  // mS/cm is a unit core converts exactly to the registry's S m-1.
  CHECK(c.meta().unit() ==
        mov::core::Unit{
            mov::core::ConductivityUnit::millisiemens_per_centimeter});
  CHECK(conductivity.warnings.empty());

  const auto salinity =
      parsed(coops_fixture("salinity.json"), met(cc::Met::salinity));
  REQUIRE(salinity.value.columns().size() == 1);
  const TimeSeries& s = column(salinity, "salinity");
  CHECK(values_of(s) == std::vector<Opt>{28.93, 28.93, 28.92});
  CHECK(s.meta().quantity() == mov::core::QuantityId{Quantity::salinity});
  CHECK(s.meta().label() == "Salinity");
  // PSU is practical salinity, the dimensionless "1".
  CHECK(s.meta().unit() == unit("1"));
  CHECK(salinity.warnings.empty());

  // Air gap: no datum (the API takes none), metres, sigma ignored.
  const auto gap = parsed(coops_fixture("air_gap.json"), cc::AirGap{});
  const TimeSeries& g = column(gap, "air_gap");
  CHECK(values_of(g) == std::vector<Opt>{70.134, 70.11, 70.083});
  CHECK(not g.meta().datum().has_value());
  CHECK(g.meta().unit() == unit("m"));
  CHECK(g.meta().label() == "Air gap");
  CHECK(gap.warnings.empty());
  CHECK(coops_fixture("air_gap.json") ==
        coops_fixture("air_gap_with_datum.json"));
}

TEST_CASE("a one-point series is kept", "[io][wire][coops][regression][L4]") {
  // v4 dropped any series of three points or fewer.
  const auto r = parsed(
      data_body(R"({"t":"2026-10-01 00:00", "v":"0.409", "f":"0,0,0,0"})"),
      observed());
  CHECK(values_of(column(r, "water_level")) == std::vector<Opt>{0.409});
}

TEST_CASE("an empty value is a Missing sample, nothing else is",
          "[io][wire][coops]") {
  const auto r = parsed(
      data_body(R"({"t":"2026-10-01 00:00", "s":"", "d":"157.0", "g":""},)"
                R"({"t":"2026-10-01 00:06", "s":"0", "d":"", "g":"-0.0"})"),
      met(cc::Met::wind));
  CHECK(values_of(column(r, "wind_speed")) ==
        std::vector<Opt>{std::nullopt, 0.0});
  CHECK(values_of(column(r, "wind_direction")) ==
        std::vector<Opt>{157.0, std::nullopt});
  CHECK(values_of(column(r, "wind_gust")) ==
        std::vector<Opt>{std::nullopt, -0.0});
}

TEST_CASE("an empty data array is an empty series", "[io][wire][coops]") {
  const auto r = parsed(R"({"data": []})", met(cc::Met::wind));
  CHECK(r.value.columns().size() == 3);
  CHECK(column(r, "wind_speed").empty());
  const auto p = parsed(R"({"predictions": []})", predicted());
  CHECK(column(p, "water_level_prediction").empty());
}

TEST_CASE("records out of time order are put in order and said so",
          "[io][wire][coops]") {
  const auto r = parsed(data_body(R"({"t":"2026-10-01 00:12", "v":"3"},)"
                                  R"({"t":"2026-10-01 00:00", "v":"1"},)"
                                  R"({"t":"2026-10-01 00:06", "v":"2"},)"
                                  R"({"t":"2026-10-01 00:06", "v":"9"})"),
                        observed());
  CHECK(values_of(column(r, "water_level")) == std::vector<Opt>{1, 2, 3});
  CHECK(r.warnings ==
        std::vector<Warning>{{.code = WarningCode::times_reordered,
                              .subject = "coops:water_level",
                              .count = 1},
                             {.code = WarningCode::duplicate_times_dropped,
                              .subject = "coops:water_level",
                              .count = 1},
                             {.code = WarningCode::conflicting_duplicate_times,
                              .subject = "coops:water_level",
                              .count = 1}});
}

TEST_CASE("members a record does not need are ignored", "[io][wire][coops]") {
  const auto r = parsed(
      data_body(R"({"t":"2026-10-01 00:00", "v":"1", "new":[1, {"x":2}],)"
                R"( "s": null, "q": null, "f": null})"),
      observed());
  CHECK(values_of(column(r, "water_level")) == std::vector<Opt>{1});
  CHECK(r.warnings.empty());
}

// ---- hostile bodies
// --------------------------------------------------------------

TEST_CASE("a truncated body is not JSON, where it stops", "[io][wire][coops]") {
  const std::string whole = coops_fixture("water_level.json");
  const std::string cut = whole.substr(0, 200);
  const auto r = parse(cut, observed());
  const auto e = parse_error_in(r);
  CHECK(e.code() == ParseErrc::not_json);
  CHECK(e.line() == 1);
  CHECK(e.column() == cut.size());
  CHECK(code_of(parse(whole + " trailing", observed())) == ParseErrc::not_json);
  CHECK(code_of(parse(R"({"data": [{"t": "\xff"}]})", observed())) ==
        ParseErrc::not_json);
}

TEST_CASE("a NUL byte is not the end of the body", "[io][wire][coops]") {
  // nlohmann's lexer stops at a NUL and would accept what comes before it.
  using namespace std::string_literals;
  const std::string body = R"({"data": []})"s + '\0' + "{garbage";
  const auto e = parse_error_in(parse(body, observed()));
  CHECK(e.code() == ParseErrc::not_json);
  CHECK(e.column() == body.find('\0'));
  CHECK(std::holds_alternative<mov::io::wire::Payload>(wc::classify_body(
      wc::Api::data, R"({"error": "Range Limit Exceeded"})"s + '\0', "x")));
}

TEST_CASE("an empty body", "[io][wire][coops]") {
  CHECK(code_of(parse("", observed())) == ParseErrc::empty_input);
  CHECK(code_of(parse(" \r\n\t", observed())) == ParseErrc::empty_input);
}

TEST_CASE("wrong JSON types are bad members, at the value",
          "[io][wire][coops]") {
  // A number where CO-OPS writes a string.
  const std::string body = data_body(R"({"t":"2026-10-01 00:00", "v":0.409})");
  const auto e = parse_error_in(parse(body, observed()));
  CHECK(e.code() == ParseErrc::bad_member);
  CHECK(e.column() == body.find("0.409"));
  CHECK(e.context().starts_with("0.409}"));
  CHECK(code_of(parse(data_body(R"({"t":5, "v":"1"})"), observed())) ==
        ParseErrc::bad_member);
  CHECK(code_of(parse(data_body(R"({"t":"2026-10-01 00:00", "v":"1", "f":0})"),
                      observed())) == ParseErrc::bad_member);
  CHECK(code_of(parse(data_body(R"({"t":"2026-10-01 00:00", "v":"1", "q":[]})"),
                      observed())) == ParseErrc::bad_member);
  CHECK(code_of(parse(data_body(R"(5)"), observed())) == ParseErrc::bad_member);
  CHECK(code_of(parse(data_body(R"([1])"), observed())) ==
        ParseErrc::bad_member);
  CHECK(code_of(parse(data_body(R"("x")"), observed())) ==
        ParseErrc::bad_member);
  CHECK(code_of(parse(R"({"data": {}})", observed())) == ParseErrc::bad_member);
  CHECK(code_of(parse(R"({"data": null})", observed())) ==
        ParseErrc::bad_member);
  CHECK(code_of(parse(R"([{"t":"2026-10-01 00:00", "v":"1"}])", observed())) ==
        ParseErrc::bad_member);
  CHECK(code_of(parse("5", observed())) == ParseErrc::bad_member);
}

TEST_CASE("missing members are bad members, at the record",
          "[io][wire][coops]") {
  const std::string body = data_body(R"({"t":"2026-10-01 00:00"})");
  const auto e = parse_error_in(parse(body, observed()));
  CHECK(e.code() == ParseErrc::bad_member);
  CHECK(e.column() == body.find("{\"t\""));
  CHECK(code_of(parse(data_body(R"({"v":"1"})"), observed())) ==
        ParseErrc::bad_member);
  // Wind needs all three of its columns.
  CHECK(
      code_of(parse(data_body(R"({"t":"2026-10-01 00:00", "s":"1", "d":"2"})"),
                    met(cc::Met::wind))) == ParseErrc::bad_member);
  // The envelope of the other kind.
  CHECK(code_of(parse(coops_fixture("predictions.json"), observed())) ==
        ParseErrc::bad_member);
  CHECK(code_of(parse(coops_fixture("water_level.json"), predicted())) ==
        ParseErrc::bad_member);
}

TEST_CASE("CO-OPS error bodies are not data", "[io][wire][coops]") {
  for (const std::string_view name :
       {"error_no_data.json", "error_range.json", "error_station.json",
        "gateway_error.json"}) {
    INFO(name);
    CHECK(code_of(parse(coops_fixture(name), observed())) ==
          ParseErrc::bad_member);
  }
  CHECK(code_of(parse(coops_fixture("error_bad_product.txt"), observed())) ==
        ParseErrc::not_json);
}

TEST_CASE("bad times are bad dates, at the character", "[io][wire][coops]") {
  const std::string body = data_body(R"({"t":"2026-10-01 24:00", "v":"1"})");
  const auto e = parse_error_in(parse(body, observed()));
  CHECK(e.code() == ParseErrc::bad_date);
  CHECK(e.column() == body.find("24:00"));
  for (const std::string_view t :
       {"", "2026-10-01T00:00+01:00", "2026-13-01 00:00", "01/10/2026 00:00",
        "2026-02-30 00:00", " 2026-10-01 00:00", "2026-10-01T00:00",
        "2026-10-01 00:00Z", "2026-10-01 00:00:00", "2026-10-01"}) {
    INFO(t);
    CHECK(code_of(
              parse(data_body(R"({"t":")" + std::string{t} + R"(", "v":"1"})"),
                    observed())) == ParseErrc::bad_date);
  }
}

TEST_CASE("NaN and infinity strings are not numbers", "[io][wire][coops]") {
  for (const std::string_view v : {"NaN", "nan", "inf", "-Infinity", "0x1p3",
                                   "1,5", " 1", "1.5x", "-", "--1", "1e"}) {
    INFO(v);
    CHECK(code_of(parse(data_body(R"({"t":"2026-10-01 00:00", "v":")" +
                                  std::string{v} + R"("})"),
                        observed())) == ParseErrc::bad_number);
  }
}

TEST_CASE("huge numbers", "[io][wire][coops]") {
  // As text: past a double is out of range, the largest double is a value.
  CHECK(code_of(parse(data_body(R"({"t":"2026-10-01 00:00", "v":"1e400"})"),
                      observed())) == ParseErrc::out_of_range);
  CHECK(code_of(parse(data_body(R"({"t":"2026-10-01 00:00", "v":"1e-400"})"),
                      observed())) == ParseErrc::out_of_range);
  const auto big = parsed(
      data_body(R"({"t":"2026-10-01 00:00", "v":"1.7976931348623157e308"})"),
      observed());
  CHECK(values_of(column(big, "water_level")) ==
        std::vector<Opt>{1.7976931348623157e308});
  // As a JSON number, even in a member nobody reads, nlohmann refuses it.
  CHECK(code_of(
            parse(data_body(R"({"t":"2026-10-01 00:00", "v":"1", "s":1e400})"),
                  observed())) == ParseErrc::out_of_range);
  // A 600-digit number is still one number.
  const auto long_digits =
      parsed(data_body(R"({"t":"2026-10-01 00:00", "v":"1)" +
                       std::string(300, '0') + R"(e-300"})"),
             observed());
  CHECK(values_of(column(long_digits, "water_level")) == std::vector<Opt>{1.0});
}

TEST_CASE("repeated members are refused, not collapsed", "[io][wire][coops]") {
  const std::string record = R"({"t":"2026-10-01 00:00", "v":"1", "v":"2"})";
  const std::string body = data_body(record);
  const auto e = parse_error_in(parse(body, observed()));
  CHECK(e.code() == ParseErrc::bad_member);
  CHECK(e.column() == body.find(record));
  CHECK(code_of(parse(R"({"data": [], "data": []})", observed())) ==
        ParseErrc::bad_member);
  // Members the format does not read are not read for repeats either.
  CHECK(parse(R"({"metadata": {"id": "1", "id": "2"}, "data": []})", observed())
            .has_value());
  // The same name in different objects is fine.
  CHECK(parse(data_body(R"({"t":"2026-10-01 00:00", "v":"1", "x":{"v":"2"}})"),
              observed())
            .has_value());
}

TEST_CASE("deep nesting is refused before the parser", "[io][wire][coops]") {
  const std::string deep =
      R"({"data": [{"t":"2026-10-01 00:00", "v":"1", "x":)" +
      std::string(20, '[') + std::string(20, ']') + "}]}";
  CHECK(code_of(parse(deep, observed())) == ParseErrc::too_large);
}

TEST_CASE("limits are checked before the samples are kept",
          "[io][wire][coops]") {
  ReadContext small;
  small.limits.max_elements = 5;
  // Six records of one column: the sixth is one too many.
  const auto r = parse(coops_fixture("water_level.json"), observed(), small);
  const auto e = parse_error_in(r);
  CHECK(e.code() == ParseErrc::too_large);
  CHECK(e.context() == "6 elements, limit 5");
  // Wind counts three samples a record.
  CHECK(code_of(parse(coops_fixture("wind.json"), met(cc::Met::wind), small)) ==
        ParseErrc::too_large);
  small.limits.max_elements = 18;
  CHECK(
      parse(coops_fixture("wind.json"), met(cc::Met::wind), small).has_value());
  ReadContext tiny;
  tiny.limits.max_text_bytes = 10;
  CHECK(code_of(parse(coops_fixture("water_level.json"), observed(), tiny)) ==
        ParseErrc::too_large);
}

TEST_CASE("a stop request cancels the parse", "[io][wire][coops]") {
  ReadContext ctx;
  ctx.stop = mov::core::StopToken{[] { return true; }};
  const auto r = parse(coops_fixture("water_level.json"), observed(), ctx);
  REQUIRE(not r.has_value());
  CHECK(std::holds_alternative<mov::core::Cancelled>(r.error()));
}

TEST_CASE("wind records out of order: one report about the product",
          "[io][wire][coops]") {
  // The columns share one time axis: a reorder or a duplicate is one event,
  // and a duplicate conflicts when any of its columns differs.
  const auto r = parsed(
      data_body(R"({"t":"2026-10-01 00:06", "s":"2", "d":"20", "g":"3"},)"
                R"({"t":"2026-10-01 00:00", "s":"1", "d":"10", "g":"2"},)"
                R"({"t":"2026-10-01 00:06", "s":"2", "d":"20", "g":"9"},)"
                R"({"t":"2026-10-01 00:00", "s":"1", "d":"10", "g":"2"})"),
      met(cc::Met::wind));
  CHECK(times_of(column(r, "wind_speed")) == six_minute_times(2));
  CHECK(values_of(column(r, "wind_speed")) == std::vector<Opt>{1, 2});
  CHECK(values_of(column(r, "wind_direction")) == std::vector<Opt>{10, 20});
  CHECK(values_of(column(r, "wind_gust")) == std::vector<Opt>{2, 3});
  CHECK(r.warnings ==
        std::vector<Warning>{{.code = WarningCode::times_reordered,
                              .subject = "coops:wind",
                              .count = 2},
                             {.code = WarningCode::duplicate_times_dropped,
                              .subject = "coops:wind",
                              .count = 2},
                             {.code = WarningCode::conflicting_duplicate_times,
                              .subject = "coops:wind",
                              .count = 1}});
}

TEST_CASE("a time is exactly yyyy-MM-dd HH:mm", "[io][wire][coops]") {
  const std::string body = data_body(R"({"t":"2026-10-01T00:00", "v":"1"})");
  const auto e = parse_error_in(parse(body, observed()));
  CHECK(e.code() == ParseErrc::bad_date);
  CHECK(e.column() == body.find("T00:00"));
  const std::string longer =
      data_body(R"({"t":"2026-10-01 00:00:00", "v":"1"})");
  CHECK(parse_error_in(parse(longer, observed())).column() ==
        longer.find(":00\""));
}

TEST_CASE("read_data classifies before it parses", "[io][wire][coops]") {
  const auto read = [](std::string_view body) {
    return wc::read_data(body, "application/json", observed(), ReadContext{});
  };
  const auto data = read(coops_fixture("water_level.json"));
  REQUIRE(data.has_value());
  const auto* columns = std::get_if<Read<mov::io::Part>>(&*data);
  REQUIRE(columns != nullptr);
  CHECK(columns->value.columns().size() == 1);
  CHECK(columns->value.columns().contains(
      mov::io::ColumnKey{.quantity = Quantity::water_level}));

  const auto none = read(coops_fixture("error_no_data.json"));
  REQUIRE(none.has_value());
  CHECK(std::holds_alternative<mov::io::wire::NoData>(*none));

  const auto refused = read(coops_fixture("error_range.json"));
  REQUIRE(refused.has_value());
  const auto* error = std::get_if<mov::io::wire::ServiceError>(&*refused);
  REQUIRE(error != nullptr);
  CHECK(error->code() == mov::io::wire::ServiceErrc::range_exceeded);
  CHECK(error->origin() == mov::io::wire::Origin::coops_data);

  // A body that is no refusal and no data is the parser's error.
  const auto broken = read(R"({"data": [{"t": 5}]})");
  CHECK(code_of(broken.transform([](auto&&) {
          return Read<mov::io::Part>{};
        })) == ParseErrc::bad_member);
}
