// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// ndbc::read_active_stations and its subset scanner: the recorded list
// (golden), the subset's edges, and everything outside it, which must fail
// closed (docs/providers-design.md section 4.5). The QXmlStreamReader
// comparison is test_ndbc_xml_reference.cpp (label qt).

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "gzip_test_support.hpp"
#include "mov/core/stop_token.hpp"
#include "mov/core/units.hpp"
#include "mov/io/detail/ndbc_xml.hpp"
#include "mov/io/error.hpp"
#include "mov/io/gzip.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/ndbc.hpp"
#include "wire_test_support.hpp"

namespace {

namespace wn = mov::io::wire::ndbc;
namespace xml = mov::io::detail::ndbc_xml;
using mov::io::ParseErrc;
using mov::io::ReadContext;
using mov::io::WarningCode;
using mov::test::parse_error_in;

std::string ndbc_fixture(std::string_view name) {
  std::string bytes =
      mov::test::read_bytes(mov::test::fixture("io/ndbc/" + std::string{name}));
  REQUIRE(not bytes.empty());
  return bytes;
}

std::expected<xml::Document, mov::io::Error> scan(std::string_view text,
                                                  const ReadContext& ctx = {}) {
  return xml::scan(text, ctx);
}

xml::Document scanned(std::string_view text) {
  auto r = scan(text);
  if (not r) {
    const auto* p = std::get_if<mov::io::ParseError>(&r.error());
    INFO((p != nullptr ? p->context() : std::string{"not a ParseError"}));
    REQUIRE(r.has_value());
  }
  return *std::move(r);
}

mov::io::Read<std::vector<wn::StationRecord>> records(std::string_view text) {
  auto r = wn::read_active_stations(text, {});
  REQUIRE(r.has_value());
  return *std::move(r);
}

const wn::StationRecord& station(const std::vector<wn::StationRecord>& all,
                                 std::string_view id) {
  const auto found = std::ranges::find_if(
      all, [&](const auto& r) { return r.station.id.value() == id; });
  REQUIRE(found != all.end());
  return *found;
}

std::size_t skipped(const mov::io::Read<std::vector<wn::StationRecord>>& r,
                    std::string_view reason) {
  for (const auto& w : r.warnings) {
    if (w.code == WarningCode::station_skipped and w.subject == reason) {
      return w.count;
    }
  }
  return 0;
}

std::string one_station(std::string_view attributes) {
  return "<stations>\n  <station " + std::string{attributes} +
         "/>\n</stations>";
}

std::string valid_attributes(std::string_view extra = "") {
  return R"(id="41002" lat="33.36" lon="-76.34" )" + std::string{extra};
}

}  // namespace

// ---- the recorded list
// -------------------------------------------------------------

TEST_CASE("the recorded list, trimmed: every kind of station",
          "[io][wire][ndbc]") {
  const auto r = records(ndbc_fixture("activestations.xml"));
  // Every station is a record: the four- and seven-character ids too
  // (owner decision 2026-10-16).
  CHECK(r.value.size() == 15);
  CHECK(r.warnings.empty());
  CHECK(station(r.value, "KATP").caps.program.view() == "Marine METAR");
  CHECK(station(r.value, "CWCI").caps.elevation ==
        mov::core::Length::in(163.0, mov::core::LengthUnit::meter));
  CHECK(station(r.value, "1801589").caps.platform_type.view() == "usv");

  const auto& hatteras = station(r.value, "41002");
  CHECK(hatteras.station.name.view() ==
        "SOUTH HATTERAS - 225 NM South of Cape Hatteras");
  CHECK(hatteras.station.location.lat() == 33.3606);
  CHECK(hatteras.station.location.lon() == -76.3382);
  CHECK(hatteras.station.validity == mov::core::ValidRange{});
  CHECK(hatteras.caps.owner.view() == "NDBC");
  CHECK(hatteras.caps.program.view() == "NDBC Meteorological/Ocean");
  CHECK(hatteras.caps.platform_type.view() == "buoy");
  CHECK(hatteras.caps.elevation ==
        mov::core::Length::in(0.0, mov::core::LengthUnit::meter));
  CHECK(hatteras.feeds == wn::Feeds{.met = false,
                                    .currents = false,
                                    .water_quality = false,
                                    .dart = false});

  CHECK(station(r.value, "14041").station.name.empty());
  CHECK_FALSE(station(r.value, "22101").caps.elevation.has_value());
  const auto& tao = station(r.value, "32303");
  CHECK(tao.feeds == wn::Feeds{});
  CHECK(tao.caps.platform_type.view() == "tao");
  CHECK(station(r.value, "32ST0").feeds.met == true);  // listed as 32st0
  CHECK(station(r.value, "42092")
            .caps.owner.view()
            .ends_with("Texas A&M University-Corpus Christi"));
  CHECK(station(r.value, "44098").station.name.view() ==
        "Jeffrey's Ledge, NH (160)");
  CHECK(station(r.value, "62114").station.name.view() == "Tartan \"A\" AWS");
  CHECK(station(r.value, "13001").station.location.lat() == 12.0);
}

TEST_CASE("the whole recorded list", "[io][wire][ndbc]") {
  const std::string gz = ndbc_fixture("activestations_full.xml.gz");
  const auto text =
      mov::io::gunzip(mov::test::bytes_of(gz), std::size_t{1} << 20);
  REQUIRE(text.has_value());
  const auto document = scanned(*text);
  CHECK(document.stations.size() == 1354);
  CHECK(document.root ==
        std::vector<xml::Attribute>{
            {.name = "created", .value = "2026-10-15T23:00:05UTC"},
            {.name = "count", .value = "1354"}});
  CHECK(document.stations.front().line == 3);
  CHECK(document.stations.back().line == 1356);
  const auto r = records(*text);
  CHECK(r.value.size() == 1354);
  CHECK(r.warnings.empty());
  // DART buoys are listed with the feeds the list gives them; seven-digit
  // ones say "n" to every feed (provenance.txt: they serve no file).
  const auto& dart = station(r.value, "5401000");
  CHECK(dart.caps.platform_type.view() == "dart");
  CHECK(dart.feeds == wn::Feeds{.met = false,
                                .currents = false,
                                .water_quality = false,
                                .dart = false});
}

// ---- the subset
// ---------------------------------------------------------------------

TEST_CASE("what the subset accepts", "[io][wire][ndbc]") {
  for (const std::string_view text :
       {"<stations/>", "<stations></stations>", "<stations >\n</stations >",
        "<?xml version=\"1.0\"?><stations/>",
        "<?xml version='1.0' encoding='UTF-8' standalone='yes'?>\n<stations/>",
        R"(<?xml version="1.0" encoding="utf-8" ?><stations/>)",
        "\xEF\xBB\xBF<?xml version=\"1.0\"?><stations/>",
        "<!-- a -->\n<stations><!--b--><!----></stations><!-- c -->\n\n",
        "<stations\r\n count = '1'\t/>"}) {
    INFO(text);
    CHECK(scan(text).has_value());
  }
}

TEST_CASE("attribute values: quotes, entities, references, '>'",
          "[io][wire][ndbc]") {
  const auto d = scanned(
      "<stations><station a=\"x'y\" b='x\"y' c=\"&lt;&gt;&amp;&quot;&apos;\" "
      "d=\"&#65;&#x42;&#x00e9;&#8364;&#x10FFFF;\" e=\"a>b\" "
      "f=\"\"/></stations>");
  REQUIRE(d.stations.size() == 1);
  CHECK(d.stations[0].attributes ==
        std::vector<xml::Attribute>{
            {.name = "a", .value = "x'y"},
            {.name = "b", .value = "x\"y"},
            {.name = "c", .value = "<>&\"'"},
            {.name = "d", .value = "AB\xC3\xA9\xE2\x82\xAC\xF4\x8F\xBF\xBF"},
            {.name = "e", .value = "a>b"},
            {.name = "f", .value = ""}});
}

TEST_CASE("references to a tab, LF or CR are those characters",
          "[io][wire][ndbc]") {
  // XML normalizes a literal one to a space (so the subset refuses it), but
  // not one written as a reference (found by fuzz_ndbc_active_stations).
  const auto d = scanned("<stations a=\"x&#9;&#10;&#13;y&#xA;\"/>");
  CHECK(d.root[0].value == "x\t\n\ry\n");
}

TEST_CASE("UTF-8 in a value is kept as it is", "[io][wire][ndbc]") {
  const auto d = scanned(
      "<stations><station name=\"S\xC3\xA3o \xF0\x9F\x8C\x8A\"/></stations>");
  CHECK(d.stations[0].attributes[0].value == "S\xC3\xA3o \xF0\x9F\x8C\x8A");
}

TEST_CASE("each station knows its line", "[io][wire][ndbc]") {
  const auto d = scanned(
      "<stations>\n<station id=\"a\"/>\r\n\n  <station\nid=\"b\"/><station "
      "id=\"c\"/>\n</stations>");
  REQUIRE(d.stations.size() == 3);
  CHECK(d.stations[0].line == 2);
  CHECK(d.stations[1].line == 4);
  CHECK(d.stations[2].line == 5);
}

TEST_CASE("everything outside the subset fails closed, where it is",
          "[io][wire][ndbc]") {
  struct Case {
    std::string_view text;
    std::size_t offset;  // where the error is, as line 1's column
  };
  for (const Case c : {
           Case{.text = "<!DOCTYPE stations><stations/>", .offset = 0},
           Case{.text = "<stations><![CDATA[x]]></stations>", .offset = 10},
           Case{.text = "<?php echo 1 ?><stations/>", .offset = 0},
           Case{.text = " <?xml version=\"1.0\"?><stations/>", .offset = 1},
           Case{.text = "<?xml version=\"1.1\"?><stations/>", .offset = 0},
           Case{.text = "<?xml encoding=\"UTF-8\"?><stations/>", .offset = 0},
           Case{.text =
                    R"(<?xml version="1.0" encoding="ISO-8859-1"?><stations/>)",
                .offset = 0},
           Case{.text = R"(<?xml standalone="yes" version="1.0"?><stations/>)",
                .offset = 0},
           Case{.text = "<?xml?><stations/>", .offset = 0},
           Case{.text = "<?xmlversion=\"1.0\"?><stations/>", .offset = 5},
           Case{.text = "<stations>hello</stations>", .offset = 10},
           Case{.text = "<stations><station><x/></station></stations>",
                .offset = 19},
           Case{.text = "<stations><station id=\"1\"></station></stations>",
                .offset = 26},
           Case{.text = "<stations><foo/></stations>", .offset = 10},
           Case{.text = "<stations><stationx/></stations>", .offset = 10},
           Case{.text = "<stations/><stations/>", .offset = 11},
           Case{.text = "<station/>", .offset = 0},
           Case{.text = "<stations>", .offset = 10},
           Case{.text = "<stations></station>", .offset = 12},
           Case{.text = "<stations></stations", .offset = 20},
           Case{.text = "<stations a/>", .offset = 11},
           Case{.text = "<stations a=1/>", .offset = 12},
           Case{.text = R"(<stations a="1" a="2"/>)", .offset = 16},
           Case{.text = "<stations xmlns=\"x\"/>", .offset = 10},
           Case{.text = "<stations XmL-x=\"x\"/>", .offset = 10},
           Case{.text = "<stations a:b=\"x\"/>", .offset = 11},
           Case{.text = R"(<stations a="1"b="2"/>)", .offset = 15},
           Case{.text = "<stations a=\"<\"/>", .offset = 13},
           Case{.text = "<stations a=\"\t\"/>", .offset = 13},
           Case{.text = "<stations a=\"\n\"/>", .offset = 13},
           Case{.text = "<stations a=\"&nbsp;\"/>", .offset = 13},
           Case{.text = "<stations a=\"&amp\"/>", .offset = 13},
           Case{.text = "<stations a=\"&#0;\"/>", .offset = 13},
           Case{.text = "<stations a=\"&#xD800;\"/>", .offset = 13},
           Case{.text = "<stations a=\"&#xFFFE;\"/>", .offset = 13},
           Case{.text = "<stations a=\"&#x110000;\"/>", .offset = 13},
           Case{.text = "<stations a=\"&#;\"/>", .offset = 13},
           Case{.text = "<stations a=\"&#x;\"/>", .offset = 13},
           Case{.text = "<stations a=\"&#xG;\"/>", .offset = 13},
           Case{.text = "<stations a=\"&#00000065;\"/>", .offset = 13},
           Case{.text = "<stations a=\"x/>", .offset = 16},
           Case{.text = "<stations><!-- a -- b --></stations>", .offset = 17},
           Case{.text = "<stations><!-- a ---></stations>", .offset = 17},
           Case{.text = "<stations><!--- -></stations>", .offset = 29},
           Case{.text = "<stations><!-- open", .offset = 19},
           Case{.text = "<stations/>trailing", .offset = 11},
           Case{.text = "< stations/>", .offset = 0},
           Case{.text = "<stations/ >", .offset = 9},
       }) {
    INFO(c.text);
    const auto r = scan(c.text);
    CHECK(parse_error_in(r).code() == ParseErrc::corrupt_record);
    CHECK(parse_error_in(r).line() == 1);
    CHECK(parse_error_in(r).column() == c.offset);
  }
}

TEST_CASE("bytes that are not XML characters", "[io][wire][ndbc]") {
  using namespace std::string_view_literals;
  for (const std::string_view text :
       {"<stations a=\"\xFF\"/>"sv, "<stations a=\"\xC3\"/>"sv,
        "<stations a=\"\x01\"/>"sv, "<stations a=\"\0\"/>"sv,
        "<stations a=\"\xEF\xBF\xBE\"/>"sv, "<stations a=\"\xEF\xBF\xBF\"/>"sv,
        "<stations a=\"\xED\xA0\x80\"/>"sv, "<stations/>\x7F\x01"sv}) {
    const auto r = scan(text);
    CHECK(parse_error_in(r).code() == ParseErrc::corrupt_record);
  }
  CHECK(parse_error_in(scan("<stations a=\"\x01\"/>")).column() == 13);
}

TEST_CASE("blank and empty texts", "[io][wire][ndbc]") {
  CHECK(parse_error_in(scan("")).code() == ParseErrc::empty_input);
  CHECK(parse_error_in(scan(" \r\n\t")).code() == ParseErrc::empty_input);
  CHECK(parse_error_in(scan("\xEF\xBB\xBF")).code() == ParseErrc::empty_input);
}

TEST_CASE("limits and cancellation", "[io][wire][ndbc]") {
  std::string many = "<stations ";
  for (std::size_t i = 0; i <= xml::max_attributes; ++i) {
    many += "a" + std::to_string(i) + "=\"\" ";
  }
  many += "/>";
  CHECK(parse_error_in(scan(many)).code() == ParseErrc::too_large);

  const std::string two =
      R"(<stations><station id="a"/><station id="b"/></stations>)";
  ReadContext ctx;
  ctx.limits.max_elements = 2 * xml::elements_per_station - 1;
  CHECK(parse_error_in(scan(two, ctx)).code() == ParseErrc::too_large);
  ctx.limits.max_elements = 2 * xml::elements_per_station;
  CHECK(scan(two, ctx).has_value());

  ReadContext small;
  small.limits.max_text_bytes = two.size() - 1;
  CHECK(parse_error_in(scan(two, small)).code() == ParseErrc::too_large);

  ReadContext stop;
  stop.stop = mov::core::StopToken{[] { return true; }};
  const auto r = scan(two, stop);
  REQUIRE(not r.has_value());
  CHECK(std::holds_alternative<mov::io::Cancelled>(r.error()));
  int polls = 0;
  ReadContext later;
  later.stop = mov::core::StopToken{[&polls] { return ++polls > 1; }};
  const auto stopped = scan(two, later);
  REQUIRE(not stopped.has_value());
  CHECK(std::holds_alternative<mov::io::Cancelled>(stopped.error()));
}

// ---- stations left out
// ------------------------------------------------------------

TEST_CASE("stations that are not records are counted by reason",
          "[io][wire][ndbc]") {
  const std::string text =
      "<stations>\n"
      "<station lat=\"1\" lon=\"2\"/>\n"                          // no id
      "<station id=\"TOOLONG1\" lat=\"1\" lon=\"2\"/>\n"          // id
      "<station id=\"41002\" lat=\"1\" lon=\"2\"/>\n"             // kept
      "<station id=\"41002\" lat=\"3\" lon=\"4\"/>\n"             // duplicate
      "<station id=\"41O02\" lat=\"x\" lon=\"2\"/>\n"             // position
      "<station id=\"41003\" lat=\"91\" lon=\"2\"/>\n"            // position
      "<station id=\"41004\" lat=\"1\"/>\n"                       // position
      "<station id=\"41005\" lat=\"1\" lon=\"2\" elev=\"-\"/>\n"  // elevation
      "<station id=\"41006\" lat=\"1\" lon=\"2\" met=\"Y\"/>\n"   // feeds
      "<station id=\"41007\" lat=\"1\" lon=\"2\" dart=\"\"/>\n"   // feeds
      "<station id=\"41008\" lat=\"1\" lon=\"359\" elev=\"-4.5\" met=\"y\" "
      "extra=\"ignored\"/>\n"
      "</stations>";
  const auto r = records(text);
  REQUIRE(r.value.size() == 2);
  CHECK(r.value[0].station.location.lat() == 1.0);  // the first 41002 wins
  const auto& last = r.value[1];
  CHECK(last.station.location.lon() == -1.0);
  CHECK(last.caps.elevation ==
        mov::core::Length::in(-4.5, mov::core::LengthUnit::meter));
  CHECK(last.feeds == wn::Feeds{.met = true,
                                .currents = std::nullopt,
                                .water_quality = std::nullopt,
                                .dart = std::nullopt});
  CHECK(last.station.name.empty());
  REQUIRE(r.warnings.size() == 5);
  CHECK(skipped(r, "id") == 2);
  CHECK(skipped(r, "duplicate") == 1);
  CHECK(skipped(r, "position") == 3);
  CHECK(skipped(r, "elevation") == 1);
  CHECK(skipped(r, "feeds") == 2);
  // Reported in a fixed order: id, duplicate, position, elevation, feeds.
  CHECK(r.warnings[0].subject == "id");
  CHECK(r.warnings[4].subject == "feeds");
}

TEST_CASE("a list without stations is an empty list", "[io][wire][ndbc]") {
  CHECK(records("<stations/>").value.empty());
  CHECK(records(one_station(valid_attributes())).value.size() == 1);
}

TEST_CASE("a scan error is the read's error", "[io][wire][ndbc]") {
  const auto r = wn::read_active_stations(
      one_station(valid_attributes("name=\"&bad;\"")), {});
  CHECK(parse_error_in(r).code() == ParseErrc::corrupt_record);
  CHECK(parse_error_in(r).line() == 2);
}
