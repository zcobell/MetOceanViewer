// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// usgs::read_continuous and read_daily: the recorded pages (goldens) and
// hostile ones (docs/providers-design.md sections 4.3, 4.9 and 10.1).

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/stop_token.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"
#include "mov/io/detail/usgs_json.hpp"
#include "mov/io/error.hpp"
#include "mov/io/part.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/service.hpp"
#include "mov/io/wire/usgs.hpp"
#include "usgs_test_support.hpp"
#include "wire_test_support.hpp"

namespace {

namespace wu = mov::io::wire::usgs;
using mov::core::Quantity;
using mov::core::QuantityId;
using mov::core::Time;
using mov::core::TimeSeries;
using mov::core::VerticalDatum;
using mov::io::Error;
using mov::io::ParseErrc;
using mov::io::Read;
using mov::io::ReadContext;
using mov::io::Warning;
using mov::io::WarningCode;
using mov::test::day;
using mov::test::next_url;
using mov::test::parse_error_in;
using mov::test::usgs_fixture;
using mov::test::utc;
using mov::test::values_of;
using Opt = std::optional<double>;

using ContinuousResult = std::expected<Read<wu::ContinuousPage>, Error>;
using DailyResult = std::expected<Read<wu::DailyPage>, Error>;

constexpr std::string_view continuous_items =
    "https://api.waterdata.usgs.gov/ogcapi/v1/collections/continuous/items";

ContinuousResult parse(std::string_view body, const ReadContext& ctx = {}) {
  return mov::io::detail::usgs::parse_continuous(
      body, mov::test::resolved_continuous(), ctx);
}

Read<wu::ContinuousPage> parsed(std::string_view body) {
  auto r = parse(body);
  if (not r) {
    const auto* p = std::get_if<mov::io::ParseError>(&r.error());
    INFO((p != nullptr ? p->context() : std::string{"not a ParseError"}));
    REQUIRE(r.has_value());
  }
  return *std::move(r);
}

DailyResult parse_daily(std::string_view body, const ReadContext& ctx = {}) {
  return mov::io::detail::usgs::parse_daily(body, mov::test::resolved_daily(),
                                            ctx);
}

Read<wu::DailyPage> parsed_daily(std::string_view body) {
  auto r = parse_daily(body);
  REQUIRE(r.has_value());
  return *std::move(r);
}

const TimeSeries& only_column(const Read<wu::ContinuousPage>& r) {
  REQUIRE(r.value.part.columns().size() == 1);
  return r.value.part.columns().begin()->second;
}

std::vector<Time> times_of(const TimeSeries& s) {
  return {s.times().begin(), s.times().end()};
}

std::vector<Opt> daily_values(const mov::core::DailySeries& s) {
  std::vector<Opt> out;
  for (const mov::core::Sample& sample : s.samples()) {
    out.push_back(sample.value());
  }
  return out;
}

std::vector<std::chrono::local_days> days_of(const mov::core::DailySeries& s) {
  return {s.days().begin(), s.days().end()};
}

// A page of the given features (JSON objects, comma-separated) and links.
std::string page_of(std::string_view features, std::string_view links = "[]") {
  return R"({"type":"FeatureCollection","features":[)" + std::string{features} +
         R"(],"links":)" + std::string{links} + "}";
}

// A feature with these properties.
std::string feature(std::string_view properties) {
  return R"({"type":"Feature","properties":{)" + std::string{properties} +
         R"(},"id":"x","geometry":null})";
}

std::string value_at(std::string_view time, std::string_view value) {
  return feature(R"("time":")" + std::string{time} + R"(","value":)" +
                 std::string{value});
}

std::string next_link(std::string_view href) {
  return R"([{"rel":"self","href":"x"},{"rel":"next","href":")" +
         std::string{href} + R"("}])";
}

ParseErrc code_of(const ContinuousResult& r) {
  return parse_error_in(r).code();
}

const Warning provisional_2{.code = WarningCode::provisional_samples,
                            .subject = "usgs:approval_status",
                            .count = 2};

}  // namespace

// ---- continuous goldens
// --------------------------------------------------------

TEST_CASE("a continuous page: values, meta, approval and the next link",
          "[io][usgs][continuous]") {
  const auto r = parsed(usgs_fixture("continuous_chain_page1.json"));
  const TimeSeries& q = only_column(r);
  CHECK(times_of(q) ==
        std::vector<Time>{utc(2026, 10, 1, 0, 0), utc(2026, 10, 1, 0, 5)});
  CHECK(values_of(q) == std::vector<Opt>{2700.0, 2750.0});
  CHECK(q.meta().quantity() == QuantityId{Quantity::discharge});
  CHECK(q.meta().label() == "Discharge");
  CHECK(q.meta().unit() == mov::core::parse_unit("ft3/s"));
  CHECK(not q.meta().datum().has_value());
  CHECK(r.value.extent == wu::TimeExtent{.first = utc(2026, 10, 1, 0, 0),
                                         .last = utc(2026, 10, 1, 0, 5)});
  CHECK(r.value.paging.features == 2);
  REQUIRE(r.value.paging.next.has_value());
  CHECK(next_url(r.value.paging)
            .starts_with(std::string{continuous_items} + "?cursor="));
  CHECK(next_url(r.value.paging).ends_with("&limit=2&f=json"));
  CHECK(r.warnings == std::vector<Warning>{provisional_2});
}

TEST_CASE("a cursor chain continues, and its full last page ends it",
          "[io][usgs][continuous][paging]") {
  // limit=2 over six values: three pages, then the API's offset link.
  const auto p2 = parsed(usgs_fixture("continuous_chain_page2.json"));
  CHECK(times_of(only_column(p2)) ==
        std::vector<Time>{utc(2026, 10, 1, 0, 10), utc(2026, 10, 1, 0, 15)});
  REQUIRE(p2.value.paging.next.has_value());
  const auto p3 = parsed(usgs_fixture("continuous_chain_page3.json"));
  CHECK(values_of(only_column(p3)) == std::vector<Opt>{2750.0, 2700.0});
  // Page 3 is full and its link is `offset=2` without the cursor: following
  // it reads 00:10 and 00:15 again (continuous_chain_page4_echo.json).
  CHECK(not p3.value.paging.next.has_value());
  CHECK(p3.value.paging.features == 2);
  const auto echo = parsed(usgs_fixture("continuous_chain_page4_echo.json"));
  CHECK(times_of(only_column(echo)) == times_of(only_column(p2)));
  // A single full page: its offset link would read an empty page.
  const auto full = parsed(usgs_fixture("continuous_full_last_page.json"));
  CHECK(full.value.paging.features == 3);
  CHECK(not full.value.paging.next.has_value());
  const auto after = parsed(usgs_fixture("continuous_after_offset_link.json"));
  CHECK(after.value.paging.features == 0);
  CHECK(not after.value.extent.has_value());
  CHECK(only_column(after).empty());
  // An offset link with a cursor, or a link without either, is followed.
  for (const std::string_view query :
       {"?offset=2&cursor=abc&limit=2", "?limit=2&f=json", ""}) {
    const auto r = parsed(
        page_of(value_at("2026-10-01T00:00:00Z", "\"1\""),
                next_link(std::string{continuous_items} + std::string{query})));
    CHECK(r.value.paging.next.has_value());
  }
}

TEST_CASE("UTC instants across a DST change stay on their grid",
          "[io][usgs][continuous][regression][L7]") {
  // USGS-01646500 on 2025-11-02, when EDT ended at 06:00Z.
  const auto r = parsed(usgs_fixture("continuous_dst.json"));
  const TimeSeries& q = only_column(r);
  REQUIRE(q.size() == 12);
  for (std::size_t i = 0; i < q.size(); ++i) {
    const std::chrono::milliseconds step{static_cast<std::int64_t>(i) *
                                         300'000};
    CHECK(q.times()[i] == utc(2025, 11, 2, 5, 30) + step);
  }
  CHECK(r.warnings.empty());  // Approved, no qualifier
  CHECK(not r.value.paging.next.has_value());
}

TEST_CASE("qualifiers are counted by their text", "[io][usgs][continuous]") {
  const auto r = parsed(usgs_fixture("continuous_estimated.json"));
  CHECK(only_column(r).size() == 10);
  CHECK(r.warnings ==
        std::vector<Warning>{{.code = WarningCode::qualified_samples,
                              .subject = "ESTIMATED",
                              .count = 10}});
  CHECK(r.value.paging.next.has_value());  // a cursor: more values
}

TEST_CASE("an empty page has an empty column and no extent",
          "[io][usgs][continuous]") {
  const auto r = parsed(usgs_fixture("continuous_empty.json"));
  CHECK(only_column(r).empty());
  CHECK(not r.value.extent.has_value());
  CHECK(r.value.paging == wu::Paging{.features = 0, .next = std::nullopt});
  CHECK(r.warnings.empty());
}

TEST_CASE("properties the request did not ask for are skipped",
          "[io][usgs][continuous]") {
  // Every property, and geometry (the docs' recording).
  const auto full = parsed(usgs_fixture("continuous_full_properties.json"));
  CHECK(values_of(only_column(full)) ==
        std::vector<Opt>{2700.0, 2750.0, 2750.0, 2750.0});
  const auto slim = parsed(usgs_fixture("continuous_slim_page1.json"));
  CHECK(only_column(slim).size() == 3);
  REQUIRE(slim.value.paging.next.has_value());
}

TEST_CASE(
    "the gage height carries the gage datum, an unknown parameter its "
    "generic quantity",
    "[io][usgs][continuous]") {
  const auto gage = mov::io::detail::usgs::parse_continuous(
      usgs_fixture("continuous_chain_page1.json"),
      mov::test::resolved_continuous(
          {.parameter = "00065", .name = "Gage height", .unit = "ft"}),
      {});
  REQUIRE(gage.has_value());
  const TimeSeries& wl = only_column(*gage);
  CHECK(wl.meta().quantity() == QuantityId{Quantity::water_level});
  CHECK(wl.meta().datum() == VerticalDatum::stnd);
  CHECK(wl.meta().unit() == mov::core::parse_unit("ft"));
  const auto navd = mov::io::detail::usgs::parse_continuous(
      usgs_fixture("continuous_chain_page1.json"),
      mov::test::resolved_continuous({.parameter = "62620", .unit = "ft"}), {});
  REQUIRE(navd.has_value());
  CHECK(only_column(*navd).meta().datum() == VerticalDatum::navd88);
  const auto turbidity = mov::io::detail::usgs::parse_continuous(
      usgs_fixture("continuous_chain_page1.json"),
      mov::test::resolved_continuous(
          {.parameter = "63680", .name = "Turbidity, FNU", .unit = "_FNU"}),
      {});
  REQUIRE(turbidity.has_value());
  const TimeSeries& t = only_column(*turbidity);
  CHECK(mov::core::token(t.meta().quantity()) == "usgs_63680");
  CHECK(t.meta().label() == "Turbidity, FNU");
  CHECK(not t.meta().datum().has_value());
}

TEST_CASE("read_continuous classifies before it parses",
          "[io][usgs][continuous]") {
  const auto series = mov::test::resolved_continuous();
  const auto page =
      wu::read_continuous(usgs_fixture("continuous_chain_page1.json"),
                          "application/json", series, {});
  REQUIRE(page.has_value());
  REQUIRE(std::holds_alternative<Read<wu::ContinuousPage>>(*page));
  const auto refusal = wu::read_continuous(
      usgs_fixture("error_time_envelope.json"), "application/json", series, {});
  REQUIRE(refusal.has_value());
  REQUIRE(std::holds_alternative<mov::io::wire::ServiceError>(*refusal));
  CHECK(std::get<mov::io::wire::ServiceError>(*refusal).code() ==
        mov::io::wire::ServiceErrc::range_exceeded);
  const auto broken =
      wu::read_continuous("{\"type\":", "application/json", series, {});
  REQUIRE(not broken.has_value());
  CHECK(parse_error_in(broken).code() == ParseErrc::not_json);
}

// ---- continuous: what the server could send
// ---------------------------------------

TEST_CASE("null and empty values are Missing samples",
          "[io][usgs][continuous]") {
  const auto r =
      parsed(page_of(value_at("2026-10-01T00:00:00+00:00", "null") + "," +
                     value_at("2026-10-01T00:05:00+00:00", "\"\"") + "," +
                     value_at("2026-10-01T00:10:00+00:00", "\"-1.5e2\"")));
  CHECK(values_of(only_column(r)) ==
        std::vector<Opt>{std::nullopt, std::nullopt, -150.0});
}

TEST_CASE("a page out of time order is put in order, not dropped",
          "[io][usgs][continuous][regression][L8]") {
  // v4 kept only increasing timestamps; v5 sorts and reports.
  const auto r =
      parsed(page_of(value_at("2026-10-01T00:10:00Z", "\"3\"") + "," +
                     value_at("2026-10-01T00:00:00Z", "\"1\"") + "," +
                     value_at("2026-10-01T00:05:00Z", "\"2\"") + "," +
                     value_at("2026-10-01T00:05:00Z", "\"9\"")));
  const TimeSeries& q = only_column(r);
  CHECK(values_of(q) == std::vector<Opt>{1.0, 2.0, 3.0});
  CHECK(r.value.extent == wu::TimeExtent{.first = utc(2026, 10, 1, 0, 0),
                                         .last = utc(2026, 10, 1, 0, 10)});
  CHECK(r.warnings ==
        std::vector<Warning>{{.code = WarningCode::times_reordered,
                              .subject = "usgs:continuous",
                              .count = 1},
                             {.code = WarningCode::duplicate_times_dropped,
                              .subject = "usgs:continuous",
                              .count = 1},
                             {.code = WarningCode::conflicting_duplicate_times,
                              .subject = "usgs:continuous",
                              .count = 1}});
}

TEST_CASE("a one-sample page is a series",
          "[io][usgs][continuous][regression][L4]") {
  // v4 dropped series of fewer than three points.
  const auto r = parsed(page_of(value_at("2026-10-01T00:00:00Z", "\"7\"")));
  CHECK(values_of(only_column(r)) == std::vector<Opt>{7.0});
}

TEST_CASE("qualifiers as text or list, approval as text or null",
          "[io][usgs][continuous]") {
  const auto r = parsed(
      page_of(feature(R"("time":"2026-10-01T00:00:00Z","value":"1",)"
                      R"("approval_status":"Provisional","qualifier":"ICE")") +
              "," +
              feature(R"("time":"2026-10-01T00:05:00Z","value":"1",)"
                      R"("approval_status":null,"qualifier":["ICE","EQUIP"])") +
              "," +
              feature(R"("time":"2026-10-01T00:10:00Z","value":"1",)"
                      R"("approval_status":"Approved","qualifier":[])")));
  CHECK(r.warnings ==
        std::vector<Warning>{{.code = WarningCode::provisional_samples,
                              .subject = "usgs:approval_status",
                              .count = 1},
                             {.code = WarningCode::qualified_samples,
                              .subject = "EQUIP",
                              .count = 1},
                             {.code = WarningCode::qualified_samples,
                              .subject = "ICE",
                              .count = 2}});
}

TEST_CASE("a hostile page cannot make a warning per qualifier",
          "[io][usgs][continuous]") {
  std::string features;
  for (int i = 0; i < 40; ++i) {
    features += (i == 0 ? "" : ",");
    features +=
        feature(R"("time":"2026-10-01T00:)" + std::string(i < 10 ? "0" : "") +
                std::to_string(i) + R"(:00Z","value":"1","qualifier":["Q)" +
                std::to_string(i) + R"("])");
  }
  const auto r = parsed(page_of(features));
  // Sixteen texts, and the other 24 samples under "...".
  REQUIRE(r.warnings.size() == 17);
  CHECK(r.warnings.back() == Warning{.code = WarningCode::qualified_samples,
                                     .subject = "...",
                                     .count = 24});
}

TEST_CASE("malformed continuous pages are refused where they break",
          "[io][usgs][continuous]") {
  const std::string ok = value_at("2026-10-01T00:00:00Z", "\"1\"");
  CHECK(code_of(parse("")) == ParseErrc::empty_input);
  CHECK(code_of(parse("{")) == ParseErrc::not_json);
  CHECK(code_of(parse(R"({"type":"FeatureCollection"})")) ==
        ParseErrc::bad_member);
  CHECK(code_of(parse(R"({"features":{}})")) == ParseErrc::bad_member);
  CHECK(code_of(parse(page_of("1"))) == ParseErrc::bad_member);
  CHECK(code_of(parse(page_of(R"({"properties":null})"))) ==
        ParseErrc::bad_member);
  CHECK(code_of(parse(page_of(R"({"id":"x"})"))) == ParseErrc::bad_member);
  // time
  CHECK(code_of(parse(page_of(feature(R"("value":"1")")))) ==
        ParseErrc::bad_member);
  CHECK(code_of(parse(page_of(feature(R"("time":5,"value":"1")")))) ==
        ParseErrc::bad_member);
  const auto bad_time =
      parse(page_of(value_at("2026-10-01T00:00:00", "\"1\"")));
  CHECK(parse_error_in(bad_time).code() == ParseErrc::bad_date);
  // value
  CHECK(code_of(parse(page_of(feature(R"("time":"2026-10-01T00:00:00Z")")))) ==
        ParseErrc::bad_member);
  CHECK(code_of(parse(page_of(value_at("2026-10-01T00:00:00Z", "1")))) ==
        ParseErrc::bad_member);
  CHECK(code_of(parse(page_of(value_at("2026-10-01T00:00:00Z", "\"1,5\"")))) ==
        ParseErrc::bad_number);
  CHECK(code_of(parse(page_of(value_at("2026-10-01T00:00:00Z", "\"NaN\"")))) ==
        ParseErrc::bad_number);
  CHECK(code_of(parse(page_of(value_at(
            "2026-10-01T00:00:00Z", "\"1e999\"")))) == ParseErrc::out_of_range);
  // marks
  CHECK(code_of(parse(page_of(feature(
            R"("time":"2026-10-01T00:00:00Z","value":"1","qualifier":3)")))) ==
        ParseErrc::bad_member);
  CHECK(
      code_of(parse(page_of(feature(
          R"("time":"2026-10-01T00:00:00Z","value":"1","qualifier":[3])")))) ==
      ParseErrc::bad_member);
  CHECK(
      code_of(parse(page_of(feature(
          R"("time":"2026-10-01T00:00:00Z","value":"1","approval_status":1)")))) ==
      ParseErrc::bad_member);
  // a member repeated
  CHECK(code_of(parse(page_of(feature(
            R"("time":"2026-10-01T00:00:00Z","value":"1","value":"2")")))) ==
        ParseErrc::bad_member);
  // the envelope
  CHECK(code_of(parse(R"({"features":[)" + ok + R"(],"numberReturned":2})")) ==
        ParseErrc::count_mismatch);
  CHECK(code_of(parse(R"({"features":[)" + ok + R"(],"numberReturned":-1})")) ==
        ParseErrc::bad_member);
  CHECK(code_of(parse(R"({"features":[)" + ok +
                      R"(],"numberReturned":"1"})")) == ParseErrc::bad_member);
  CHECK(
      parse(R"({"features":[)" + ok + R"(],"numberReturned":1})").has_value());
  CHECK(code_of(parse(page_of(ok, R"({"rel":"next"})"))) ==
        ParseErrc::bad_member);
  CHECK(code_of(parse(page_of(ok, R"([1])"))) == ParseErrc::bad_member);
  CHECK(code_of(parse(page_of(ok, R"([{"rel":"next"}])"))) ==
        ParseErrc::bad_member);
  CHECK(code_of(parse(page_of(ok, R"([{"rel":"next","href":7}])"))) ==
        ParseErrc::bad_member);
  const std::string link = std::string{continuous_items} + "?cursor=a";
  CHECK(code_of(parse(page_of(ok, R"([{"rel":"next","href":")" + link +
                                      R"("},{"rel":"next","href":")" + link +
                                      R"("}])"))) == ParseErrc::bad_member);
}

TEST_CASE("a next link elsewhere is refused, not followed",
          "[io][usgs][continuous][paging]") {
  const std::string ok = value_at("2026-10-01T00:00:00Z", "\"1\"");
  for (const std::string_view href :
       {"https://evil.example/ogcapi/v1/collections/continuous/items?cursor=a",
        "http://api.waterdata.usgs.gov/ogcapi/v1/collections/continuous/"
        "items?cursor=a",
        "https://api.waterdata.usgs.gov/ogcapi/v1/collections/daily/"
        "items?cursor=a",
        "https://api.waterdata.usgs.gov/ogcapi/v1/collections/continuous/"
        "items?cursor=a#x"}) {
    const auto r = parse(page_of(ok, next_link(href)));
    const auto e = mov::test::format_error_in(r);
    CHECK(e.code == mov::io::FormatErrc::foreign_link);
    CHECK(e.subject == href);
  }
}

TEST_CASE("continuous pages are bounded before they are built",
          "[io][usgs][continuous]") {
  const std::string two = value_at("2026-10-01T00:00:00Z", "\"1\"") + "," +
                          value_at("2026-10-01T00:05:00Z", "\"2\"");
  ReadContext small;
  small.limits.max_elements = 1;
  CHECK(code_of(parse(page_of(two), small)) == ParseErrc::too_large);
  small.limits.max_elements = 2;
  CHECK(parse(page_of(two), small).has_value());
  small.limits.max_text_bytes = 10;
  CHECK(code_of(parse(page_of(two), small)) == ParseErrc::too_large);
  // Nesting past max_json_depth, NUL bytes, too many links or qualifiers.
  CHECK(code_of(parse(std::string(20, '[') + std::string(20, ']'))) ==
        ParseErrc::too_large);
  CHECK(code_of(parse(page_of(two) + std::string(1, '\0'))) ==
        ParseErrc::not_json);
  std::string links = "[";
  for (std::size_t i = 0; i <= mov::io::detail::usgs::max_links; ++i) {
    links += (i == 0 ? "" : ",");
    links += R"({"rel":"alternate","href":"x"})";
  }
  CHECK(code_of(parse(page_of(two, links + "]"))) == ParseErrc::too_large);
  std::string qualifiers;
  for (int i = 0; i < 17; ++i) {
    qualifiers += (i == 0 ? "\"Q\"" : ",\"Q\"");
  }
  CHECK(code_of(parse(page_of(feature(
            R"("time":"2026-10-01T00:00:00Z","value":"1","qualifier":[)" +
            qualifiers + "]")))) == ParseErrc::too_large);
  // A leading byte order mark is skipped.
  CHECK(parse("\xEF\xBB\xBF" + page_of(two)).has_value());
}

TEST_CASE("a continuous parse stops when asked", "[io][usgs][continuous]") {
  ReadContext stopped;
  stopped.stop = mov::core::StopToken{[] { return true; }};
  const auto r = parse(usgs_fixture("continuous_dst.json"), stopped);
  REQUIRE(not r.has_value());
  CHECK(std::holds_alternative<mov::core::Cancelled>(r.error()));
}

// ---- daily
// ------------------------------------------------------------------------

TEST_CASE("a daily page: local days in order, the statistic, the meta",
          "[io][usgs][daily]") {
  const auto r = parsed_daily(usgs_fixture("daily_full_properties.json"));
  const mov::core::DailySeries& d = r.value.values;
  CHECK(days_of(d) == std::vector<std::chrono::local_days>{day(2026, 9, 28),
                                                           day(2026, 9, 29),
                                                           day(2026, 9, 30)});
  CHECK(daily_values(d) == std::vector<Opt>{3800.0, 3330.0, 2840.0});
  CHECK(d.statistic() == mov::core::usgs::StatisticId::mean());
  CHECK(d.meta().quantity() == QuantityId{Quantity::discharge});
  CHECK(d.meta().unit() == mov::core::parse_unit("cfs"));
  CHECK(r.value.paging == wu::Paging{.features = 3, .next = std::nullopt});
  CHECK(r.warnings ==
        std::vector<Warning>{{.code = WarningCode::provisional_samples,
                              .subject = "usgs:approval_status",
                              .count = 3}});
}

TEST_CASE("daily values come in no time order and are sorted silently",
          "[io][usgs][daily][paging]") {
  // limit=4 over 2026-09-20..30: eleven days on three pages, each page and
  // the pages together in feature-id order.
  const auto p1 = parsed_daily(usgs_fixture("daily_chain_page1.json"));
  CHECK(days_of(p1.value.values) == std::vector<std::chrono::local_days>{
                                        day(2026, 9, 22), day(2026, 9, 26),
                                        day(2026, 9, 27), day(2026, 9, 28)});
  CHECK(daily_values(p1.value.values) ==
        std::vector<Opt>{2640.0, 3670.0, 4470.0, 3800.0});
  REQUIRE(p1.value.paging.next.has_value());
  CHECK(next_url(p1.value.paging)
            .starts_with(
                "https://api.waterdata.usgs.gov/ogcapi/v1/collections/daily/"
                "items?cursor="));
  // No times_reordered: the order is the API's, not a fault.
  CHECK(p1.warnings ==
        std::vector<Warning>{{.code = WarningCode::provisional_samples,
                              .subject = "usgs:approval_status",
                              .count = 4}});
  const auto p2 = parsed_daily(usgs_fixture("daily_chain_page2.json"));
  CHECK(days_of(p2.value.values).front() == day(2026, 9, 20));
  REQUIRE(p2.value.paging.next.has_value());
  const auto p3 = parsed_daily(usgs_fixture("daily_chain_page3.json"));
  CHECK(days_of(p3.value.values) ==
        std::vector<std::chrono::local_days>{day(2026, 9, 23), day(2026, 9, 24),
                                             day(2026, 9, 30)});
  CHECK(not p3.value.paging.next.has_value());
}

TEST_CASE("a daily value is the site's day, not an instant",
          "[io][usgs][daily][regression][L7]") {
  // The days around the 2025-11-02 DST change are the dates as written.
  const auto r = parsed_daily(usgs_fixture("daily_dst.json"));
  CHECK(days_of(r.value.values) ==
        std::vector<std::chrono::local_days>{day(2025, 11, 1), day(2025, 11, 2),
                                             day(2025, 11, 3)});
  CHECK(daily_values(r.value.values) ==
        std::vector<Opt>{2540.0, 3040.0, 2920.0});
  // A full page of ten days whose offset link is the end.
  const auto estimated = parsed_daily(usgs_fixture("daily_estimated.json"));
  CHECK(estimated.value.values.size() == 10);
  CHECK(not estimated.value.paging.next.has_value());
  CHECK(estimated.warnings ==
        std::vector<Warning>{{.code = WarningCode::qualified_samples,
                              .subject = "ESTIMATED",
                              .count = 10}});
}

TEST_CASE("a repeated day keeps the first and says so", "[io][usgs][daily]") {
  const auto r = parsed_daily(page_of(value_at("2026-09-02", "\"2\"") + "," +
                                      value_at("2026-09-01", "\"1\"") + "," +
                                      value_at("2026-09-02", "\"5\"") + "," +
                                      value_at("2026-09-01", "\"1\"")));
  CHECK(daily_values(r.value.values) == std::vector<Opt>{1.0, 2.0});
  CHECK(r.warnings ==
        std::vector<Warning>{{.code = WarningCode::duplicate_times_dropped,
                              .subject = "usgs:daily",
                              .count = 2},
                             {.code = WarningCode::conflicting_duplicate_times,
                              .subject = "usgs:daily",
                              .count = 1}});
}

TEST_CASE("a daily time is a date and nothing else", "[io][usgs][daily]") {
  for (const std::string_view time :
       {"2026-09-01T00:00:00Z", "2026-09-01 ", "2026-9-01", "2026-02-30", "",
        "20260901"}) {
    const auto r = parse_daily(page_of(value_at(time, "\"1\"")));
    CHECK(parse_error_in(r).code() == ParseErrc::bad_date);
  }
  const auto r = parse_daily(page_of(value_at("2026-09-01x", "\"1\"")));
  // Line 1; the column is where the date stopped fitting.
  CHECK(parse_error_in(r).column().has_value());
  CHECK(parse_daily(page_of(value_at("2026-09-01", "null"))).has_value());
}

TEST_CASE("read_daily classifies before it parses", "[io][usgs][daily]") {
  const auto series = mov::test::resolved_daily();
  const auto page = wu::read_daily(usgs_fixture("daily_dst.json"),
                                   "application/json", series, {});
  REQUIRE(page.has_value());
  CHECK(std::holds_alternative<Read<wu::DailyPage>>(*page));
  const auto refusal = wu::read_daily(usgs_fixture("error_invalid_key.json"),
                                      "application/json", series, {});
  REQUIRE(refusal.has_value());
  CHECK(std::get<mov::io::wire::ServiceError>(*refusal).code() ==
        mov::io::wire::ServiceErrc::api_key_invalid);
}
