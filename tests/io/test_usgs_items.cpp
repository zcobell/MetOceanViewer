// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// usgs::read_continuous and read_daily: the recorded pages (goldens) and
// hostile ones (docs/providers-design.md sections 4.3, 4.9 and 10.1).

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
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
using mov::io::FormatErrc;
using mov::io::ParseErrc;
using mov::io::Read;
using mov::io::ReadContext;
using mov::io::Warning;
using mov::io::WarningCode;
using mov::test::day;
using mov::test::ends;
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
      body, mov::test::continuous_epoch(), ctx);
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
  return mov::io::detail::usgs::parse_daily(body, mov::test::daily_epoch(),
                                            ctx);
}

Read<wu::DailyPage> parsed_daily(std::string_view body) {
  auto r = parse_daily(body);
  REQUIRE(r.has_value());
  return *std::move(r);
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

// An extent to compare a page's with (nullopt only if inverted).
std::optional<wu::TimeExtent> extent(Time first, Time last) {
  return wu::TimeExtent::make(first, last);
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
  const TimeSeries& q = r.value.values;
  CHECK(times_of(q) ==
        std::vector<Time>{utc(2026, 10, 1, 0, 0), utc(2026, 10, 1, 0, 5)});
  CHECK(values_of(q) == std::vector<Opt>{2700.0, 2750.0});
  CHECK(q.meta().quantity() == QuantityId{Quantity::discharge});
  CHECK(q.meta().label() == "Discharge");
  CHECK(q.meta().unit() == mov::core::parse_unit("ft3/s"));
  CHECK(not q.meta().datum().has_value());
  CHECK(r.value.extent() ==
        extent(utc(2026, 10, 1, 0, 0), utc(2026, 10, 1, 0, 5)));
  CHECK(r.value.paging.features == 2);
  CHECK(next_url(r.value.paging)
            .starts_with(std::string{continuous_items} + "?cursor="));
  CHECK(next_url(r.value.paging).ends_with("&limit=2&f=json"));
  CHECK(r.warnings == std::vector<Warning>{provisional_2});
}

TEST_CASE("an extent is ordered", "[io][usgs][continuous]") {
  CHECK(wu::TimeExtent::make(utc(2026, 1, 1), utc(2026, 1, 1)).has_value());
  CHECK_FALSE(wu::TimeExtent::make(utc(2026, 1, 2), utc(2026, 1, 1)));
}

TEST_CASE("a cursor chain continues, and its full last page echoes",
          "[io][usgs][continuous][paging]") {
  // limit=2 over six values: three pages, then the API's offset link.
  const auto p2 = parsed(usgs_fixture("continuous_chain_page2.json"));
  CHECK(times_of(p2.value.values) ==
        std::vector<Time>{utc(2026, 10, 1, 0, 10), utc(2026, 10, 1, 0, 15)});
  CHECK_FALSE(next_url(p2.value.paging).empty());
  const auto p3 = parsed(usgs_fixture("continuous_chain_page3.json"));
  CHECK(values_of(p3.value.values) == std::vector<Opt>{2750.0, 2700.0});
  // Page 3 is full and its link is offset=2&limit=2 without the cursor: an
  // echo, which re-reads 00:10 and 00:15 (continuous_chain_page4_echo.json)
  // when followed from a later page. P7 follows it only from a first page.
  const auto* echo = std::get_if<wu::OffsetEcho>(&p3.value.paging.next);
  REQUIRE(echo != nullptr);
  CHECK(echo->link.url().find("offset=2") != std::string::npos);
  CHECK(p3.value.paging.features == 2);
  const auto again = parsed(usgs_fixture("continuous_chain_page4_echo.json"));
  CHECK(times_of(again.value.values) == times_of(p2.value.values));
  // A single full page: its echo would read an empty page.
  const auto full = parsed(usgs_fixture("continuous_full_last_page.json"));
  CHECK(full.value.paging.features == 3);
  CHECK(std::holds_alternative<wu::OffsetEcho>(full.value.paging.next));
  const auto after = parsed(usgs_fixture("continuous_after_offset_link.json"));
  CHECK(after.value.paging.features == 0);
  CHECK(not after.value.extent().has_value());
  CHECK(after.value.values.empty());
  CHECK(ends(after.value.paging));
  // An offset link with a cursor, or a link without either, is a Next.
  for (const std::string_view query :
       {"?offset=2&cursor=abc&limit=2", "?limit=2&f=json", ""}) {
    const auto r = parsed(
        page_of(value_at("2026-10-01T00:00:00Z", "\"1\""),
                next_link(std::string{continuous_items} + std::string{query})));
    CHECK(std::holds_alternative<wu::Next>(r.value.paging.next));
  }
}

TEST_CASE("an offset link that is no echo is refused",
          "[io][usgs][continuous][paging]") {
  const std::string one = value_at("2026-10-01T00:00:00Z", "\"1\"");
  const std::string two = one + "," + value_at("2026-10-01T00:05:00Z", "\"2\"");
  const auto unexpected = [](const ContinuousResult& r) {
    return mov::test::format_error_in(r).code == FormatErrc::unexpected_link;
  };
  const auto with_link = [&](std::string_view features,
                             std::string_view query) {
    return parse(page_of(features, next_link(std::string{continuous_items} +
                                             "?" + std::string{query})));
  };
  // A one-row page is not full at limit 2.
  CHECK(unexpected(with_link(one, "offset=2&limit=2")));
  // An offset past the page.
  CHECK(unexpected(with_link(two, "offset=100&limit=50")));
  CHECK(unexpected(with_link(two, "offset=2&limit=50")));
  CHECK(unexpected(with_link(two, "offset=2")));
  CHECK(unexpected(with_link(two, "offset=x&limit=2")));
  CHECK(unexpected(with_link(two, "offset=&limit=2")));
  // offset = limit = features: the echo.
  CHECK(std::holds_alternative<wu::OffsetEcho>(
      with_link(two, "f=json&offset=2&limit=2")->value.paging.next));
  // The recorded page whose offset link has no limit (the request had none).
  const auto no_limit =
      parse_daily(usgs_fixture("daily_offset_without_limit.json"));
  CHECK(mov::test::format_error_in(no_limit).code ==
        FormatErrc::unexpected_link);
  // A link elsewhere is refused for that first, offset or not.
  const auto foreign = parse(page_of(
      two, next_link("https://evil.example/ogcapi/v1/collections/continuous/"
                     "items?offset=2&limit=2")));
  CHECK(mov::test::format_error_in(foreign).code == FormatErrc::refused_link);
}

TEST_CASE("UTC instants across a DST change stay on their grid",
          "[io][usgs][continuous][regression][L7]") {
  // USGS-01646500 on 2025-11-02, when EDT ended at 06:00Z.
  const auto r = parsed(usgs_fixture("continuous_dst.json"));
  const TimeSeries& q = r.value.values;
  REQUIRE(q.size() == 12);
  for (std::size_t i = 0; i < q.size(); ++i) {
    const std::chrono::milliseconds step{static_cast<std::int64_t>(i) *
                                         300'000};
    CHECK(q.times()[i] == utc(2025, 11, 2, 5, 30) + step);
  }
  CHECK(r.warnings.empty());  // Approved, no qualifier
  CHECK(ends(r.value.paging));
}

TEST_CASE("local offsets across the DST change are the same instants",
          "[io][usgs][continuous][regression][L7]") {
  // The twelve instants of continuous_dst.json written as the site's local
  // time: EDT (-04:00) until 06:00Z, then EST (-05:00). 01:30-01:55 local
  // occurs twice, once in each offset.
  std::string features;
  for (int i = 0; i < 12; ++i) {
    const int utc_minutes = (5 * 60) + 30 + (5 * i);
    const bool edt = utc_minutes < 6 * 60;
    const int local = utc_minutes - (edt ? 4 * 60 : 5 * 60);
    const std::string time =
        std::format("2025-11-02T{:02}:{:02}:00{}", local / 60, local % 60,
                    edt ? "-04:00" : "-05:00");
    features += (i == 0 ? "" : ",");
    features += value_at(time, "\"" + std::to_string(i) + "\"");
  }
  const auto r = parsed(page_of(features));
  const TimeSeries& q = r.value.values;
  REQUIRE(q.size() == 12);
  for (std::size_t i = 0; i < q.size(); ++i) {
    const std::chrono::milliseconds step{static_cast<std::int64_t>(i) *
                                         300'000};
    CHECK(q.times()[i] == utc(2025, 11, 2, 5, 30) + step);
  }
  // No duplicates, no reordering: distinct instants in order.
  CHECK(r.warnings.empty());
}

TEST_CASE("qualifiers are counted by their text", "[io][usgs][continuous]") {
  const auto r = parsed(usgs_fixture("continuous_estimated.json"));
  CHECK(r.value.values.size() == 10);
  CHECK(r.warnings ==
        std::vector<Warning>{{.code = WarningCode::qualified_samples,
                              .subject = "ESTIMATED",
                              .count = 10}});
  CHECK(std::holds_alternative<wu::Next>(r.value.paging.next));  // a cursor
}

TEST_CASE("an empty page has an empty series and no extent",
          "[io][usgs][continuous]") {
  const auto r = parsed(usgs_fixture("continuous_empty.json"));
  CHECK(r.value.values.empty());
  CHECK(not r.value.extent().has_value());
  CHECK(r.value.paging == wu::Paging{.features = 0, .next = wu::NoNext{}});
  CHECK(r.warnings.empty());
}

TEST_CASE("properties the request did not ask for are skipped",
          "[io][usgs][continuous]") {
  // Every property, and geometry (the docs' recording).
  const auto full = parsed(usgs_fixture("continuous_full_properties.json"));
  CHECK(values_of(full.value.values) ==
        std::vector<Opt>{2700.0, 2750.0, 2750.0, 2750.0});
  const auto slim = parsed(usgs_fixture("continuous_slim_page1.json"));
  CHECK(slim.value.values.size() == 3);
  CHECK(std::holds_alternative<wu::Next>(slim.value.paging.next));
}

TEST_CASE(
    "the gage height carries the gage datum, an unknown parameter its "
    "generic quantity",
    "[io][usgs][continuous]") {
  const auto gage = mov::io::detail::usgs::parse_continuous(
      usgs_fixture("continuous_chain_page1.json"),
      mov::test::continuous_epoch(
          {.parameter = "00065", .name = "Gage height", .unit = "ft"}),
      {});
  REQUIRE(gage.has_value());
  const TimeSeries& wl = gage->value.values;
  CHECK(wl.meta().quantity() == QuantityId{Quantity::water_level});
  CHECK(wl.meta().datum() == VerticalDatum::stnd);
  CHECK(wl.meta().unit() == mov::core::parse_unit("ft"));
  const auto navd = mov::io::detail::usgs::parse_continuous(
      usgs_fixture("continuous_chain_page1.json"),
      mov::test::continuous_epoch({.parameter = "62620", .unit = "ft"}), {});
  REQUIRE(navd.has_value());
  CHECK(navd->value.values.meta().datum() == VerticalDatum::navd88);
  const auto turbidity = mov::io::detail::usgs::parse_continuous(
      usgs_fixture("continuous_chain_page1.json"),
      mov::test::continuous_epoch(
          {.parameter = "63680", .name = "Turbidity, FNU", .unit = "_FNU"}),
      {});
  REQUIRE(turbidity.has_value());
  const TimeSeries& t = turbidity->value.values;
  CHECK(mov::core::token(t.meta().quantity()) == "usgs_63680");
  CHECK(t.meta().label() == "Turbidity, FNU");
  CHECK(not t.meta().datum().has_value());
}

TEST_CASE("read_continuous classifies before it parses",
          "[io][usgs][continuous]") {
  const auto epoch = mov::test::continuous_epoch();
  const auto page =
      wu::read_continuous(usgs_fixture("continuous_chain_page1.json"),
                          "application/json", epoch, {});
  REQUIRE(page.has_value());
  REQUIRE(std::holds_alternative<Read<wu::ContinuousPage>>(*page));
  const auto refusal = wu::read_continuous(
      usgs_fixture("error_time_envelope.json"), "application/json", epoch, {});
  REQUIRE(refusal.has_value());
  REQUIRE(std::holds_alternative<mov::io::wire::ServiceError>(*refusal));
  CHECK(std::get<mov::io::wire::ServiceError>(*refusal).code() ==
        mov::io::wire::ServiceErrc::range_exceeded);
  const auto broken =
      wu::read_continuous("{\"type\":", "application/json", epoch, {});
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
  CHECK(values_of(r.value.values) ==
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
  CHECK(values_of(r.value.values) == std::vector<Opt>{1.0, 2.0, 3.0});
  CHECK(r.value.extent() ==
        extent(utc(2026, 10, 1, 0, 0), utc(2026, 10, 1, 0, 10)));
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
  CHECK(values_of(r.value.values) == std::vector<Opt>{7.0});
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
  struct Case {
    std::string_view href;
    std::string_view reason;
  };
  for (const Case c :
       {Case{.href = "https://evil.example/ogcapi/v1/collections/continuous/"
                     "items?cursor=a",
             .reason = "other_origin"},
        Case{.href = "http://api.waterdata.usgs.gov/ogcapi/v1/collections/"
                     "continuous/items?cursor=a",
             .reason = "other_origin"},
        Case{.href =
                 "https://api.waterdata.usgs.gov/ogcapi/v1/collections/daily/"
                 "items?cursor=a",
             .reason = "other_path"},
        Case{.href = "https://api.waterdata.usgs.gov/ogcapi/v1/collections/"
                     "continuous/items?cursor=a#x",
             .reason = "bad_character"}}) {
    const auto r = parse(page_of(ok, next_link(c.href)));
    const auto e = mov::test::format_error_in(r);
    CHECK(e.code == FormatErrc::refused_link);
    // The reason, then the link.
    CHECK(e.subject == std::string{c.reason} + " " + std::string{c.href});
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
  const auto qualifiers = [](int n) {
    std::string out;
    for (int i = 0; i < n; ++i) {
      out += (i == 0 ? "\"Q\"" : ",\"Q\"");
    }
    return out;
  };
  // 256 qualifiers on one sample are read; 257 fail the page.
  CHECK(parse(page_of(feature(
                  R"("time":"2026-10-01T00:00:00Z","value":"1","qualifier":[)" +
                  qualifiers(256) + "]")))
            .has_value());
  CHECK(code_of(parse(page_of(feature(
            R"("time":"2026-10-01T00:00:00Z","value":"1","qualifier":[)" +
            qualifiers(257) + "]")))) == ParseErrc::too_large);
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
  CHECK(r.value.paging == wu::Paging{.features = 3, .next = wu::NoNext{}});
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
  CHECK(next_url(p1.value.paging)
            .starts_with("https://api.waterdata.usgs.gov/ogcapi/v1/"
                         "collections/daily/items?cursor="));
  // No times_reordered: the order is the API's, not a fault.
  CHECK(p1.warnings ==
        std::vector<Warning>{{.code = WarningCode::provisional_samples,
                              .subject = "usgs:approval_status",
                              .count = 4}});
  const auto p2 = parsed_daily(usgs_fixture("daily_chain_page2.json"));
  CHECK(days_of(p2.value.values).front() == day(2026, 9, 20));
  CHECK(std::holds_alternative<wu::Next>(p2.value.paging.next));
  const auto p3 = parsed_daily(usgs_fixture("daily_chain_page3.json"));
  CHECK(days_of(p3.value.values) ==
        std::vector<std::chrono::local_days>{day(2026, 9, 23), day(2026, 9, 24),
                                             day(2026, 9, 30)});
  CHECK(ends(p3.value.paging));
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
  // A full page of ten days (limit=10) whose offset link is its echo.
  const auto estimated = parsed_daily(usgs_fixture("daily_estimated.json"));
  CHECK(estimated.value.values.size() == 10);
  CHECK(std::holds_alternative<wu::OffsetEcho>(estimated.value.paging.next));
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
  const auto epoch = mov::test::daily_epoch();
  const auto page = wu::read_daily(usgs_fixture("daily_dst.json"),
                                   "application/json", epoch, {});
  REQUIRE(page.has_value());
  CHECK(std::holds_alternative<Read<wu::DailyPage>>(*page));
  const auto refusal = wu::read_daily(usgs_fixture("error_invalid_key.json"),
                                      "application/json", epoch, {});
  REQUIRE(refusal.has_value());
  CHECK(std::get<mov::io::wire::ServiceError>(*refusal).code() ==
        mov::io::wire::ServiceErrc::api_key_invalid);
}
