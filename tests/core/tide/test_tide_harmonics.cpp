// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// HarmonicsFile::make and validate (WP H2): the worked example of
// docs/harmonics-json.md 13.1 resolved into the file model, every error and
// warning of docs/harmonics-json.md 10 that the model reports, their pinned
// order, and the views.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "../test_helpers.hpp"
#include "harmonics_example.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/tide/constituent_set.hpp"
#include "mov/core/tide/datums.hpp"
#include "mov/core/tide/harmonics.hpp"
#include "mov/core/tide/harmonics_input.hpp"
#include "mov/core/tide/licence.hpp"
#include "mov/core/tide/station_info.hpp"

namespace t = mov::core::tide;
using mov::core::VerticalDatum;
using mov::test::tide::compound;
using mov::test::tide::darwin;
using mov::test::tide::fernandina_beach;
using mov::test::tide::harmonics_id;
using mov::test::tide::heights;
using mov::test::tide::kings_ferry;
using mov::test::tide::metres;
using mov::test::tide::must;
using mov::test::tide::name;
using mov::test::tide::noaa_source;
using mov::test::tide::reference_of;
using mov::test::tide::row;
using mov::test::tide::subordinate_of;
using mov::test::tide::worked_example;
using mov::test::tide::ymd;
using t::DatumToken;
using t::HarmonicsError;
using t::HarmonicsFile;
using t::HarmonicsInput;
using t::HarmonicsWarning;

namespace {

/// make()'s error; fails the test if it succeeds.
HarmonicsError make_error(HarmonicsInput in) {
  auto built = HarmonicsFile::make(std::move(in));
  REQUIRE_FALSE(built.has_value());
  return built.error();
}

/// make() and validate() agree: validate's first error is make's, and both
/// accept or refuse together.
void check_agreement(const HarmonicsInput& in) {
  const auto report = t::validate(in);
  auto built = HarmonicsFile::make(in);
  REQUIRE(built.has_value() == report.ok());
  if (not built) {
    CHECK(report.errors.front() == built.error());
  } else {
    CHECK(report.warnings == built->warnings);
  }
}

double m2_speed() {
  return t::Argument::darwin({.v = {2, -2, 2, 0, 0},
                              .c_deg = 0,
                              .u = {2, -2, 0, 0, 0, 0, 0},
                              .f = t::NodeFactor::f78})
      .speed_deg_per_hour();
}

double k1_speed() {
  return t::Argument::darwin({.v = {1, 0, 1, 0, 0},
                              .c_deg = -90,
                              .u = {0, 0, -1, 0, 0, 0, 0},
                              .f = t::NodeFactor::f227})
      .speed_deg_per_hour();
}

/// The single error a fault of the example gives: validate() finds only
/// it, make() stops at it, and code() names it.
void expect_error(std::string_view what, void (*mutate)(HarmonicsInput&),
                  const HarmonicsError& error, t::HarmonicsErrc expected) {
  INFO(what);
  HarmonicsInput in = worked_example();
  mutate(in);
  const auto report = t::validate(in);
  REQUIRE(report.errors.size() == 1);
  CHECK(report.errors[0] == error);
  CHECK(t::code(report.errors[0]) == expected);
  // Not inside CHECK: Catch2 expands the expression twice.
  const HarmonicsError made = make_error(std::move(in));
  CHECK(made == error);
}

t::HarmonicsBuild build(HarmonicsInput in) {
  auto built = HarmonicsFile::make(std::move(in));
  REQUIRE(built.has_value());
  return *std::move(built);
}

t::ComputedDatumsInput computed(std::initializer_list<t::TokenHeight> rows,
                                std::chrono::year_month_day start = ymd(2007, 1,
                                                                        1),
                                std::chrono::year_month_day end = ymd(2026, 1,
                                                                      1)) {
  return t::ComputedDatumsInput{
      .start = start, .end = end, .heights = heights(rows)};
}

t::SourceInput source(std::string_view id, std::string licence) {
  t::SourceInput s = noaa_source();
  s.id = t::SourceToken::make(id).value();
  s.licence = std::move(licence);
  return s;
}

}  // namespace

TEST_CASE("the worked example resolves into the file model",
          "[core][tide][harmonics]") {
  const auto built = build(worked_example());
  const HarmonicsFile& file = built.file;

  const auto& h = file.header();
  CHECK(h.version.major_version == 1);
  CHECK(h.created == mov::core::Time{std::chrono::sys_days{ymd(2026, 10, 8)}} +
                         std::chrono::hours{12});
  CHECK(h.generator.name == "mov-harmonics-builder");
  CHECK(h.title == "Worked example (docs/harmonics-json.md section 13)");
  CHECK_FALSE(h.description);

  REQUIRE(file.sources().size() == 1);
  CHECK(file.sources()[0].id.view() == "noaa-coops");
  CHECK(file.sources()[0].retrieved == std::chrono::sys_days{ymd(2026, 10, 8)});
  CHECK(file.sources()[0].licence.identifiers().size() == 1);

  CHECK(file.constituents().size() == 7);
  CHECK(file.reference_count() == 1);
  CHECK(file.subordinate_count() == 1);

  const t::StationIndex ref_index = must(file.find("noaa-coops:8720030"));
  REQUIRE(std::holds_alternative<t::ReferenceIndex>(ref_index));
  const t::ReferenceView ref =
      file.reference(std::get<t::ReferenceIndex>(ref_index));
  CHECK(ref.info().name.view() == "Fernandina Beach, FL");
  CHECK(ref.info().location.lon() == -81.46584);
  CHECK(ref.info().time_zone == "America/New_York");
  CHECK(ref.info().datums.published_epoch() == "1983-2001");
  CHECK(ref.info().datums.height(t::TideDatum{DatumToken::mllw}) ==
        metres(-1.004));
  CHECK(ref.info().datums.table().height_above_msl(VerticalDatum::navd88) ==
        metres(0.161));
  CHECK(ref.z0() == metres(0.0));
  CHECK(ref.analysis() == t::Analysis::nos);
  CHECK(&ref.constituents() == &file.constituents());
  CHECK(ref.licence().text() == "LicenseRef-US-Government-Public-Domain");
  CHECK(ref.source().id.view() == "noaa-coops");
  CHECK(&ref.source() == &file.source(ref.info().source));
  // The rows keep the file's order and point into the file's set.
  const std::vector<std::string_view> rows{"M2", "S2", "N2", "K1",
                                           "O1", "M4", "MS4"};
  REQUIRE(ref.constants().size() == rows.size());
  for (std::size_t i = 0; i < rows.size(); ++i) {
    CHECK(file.constituents().name(ref.constants()[i].constituent).view() ==
          rows[i]);
  }
  CHECK(ref.constants()[0].value.amplitude() == metres(0.87));
  CHECK(ref.constants()[0].value.phase().degrees() == 33.7);

  const t::StationIndex sub_index = must(file.find("noaa-coops:8720001"));
  REQUIRE(std::holds_alternative<t::SubordinateIndex>(sub_index));
  const t::SubordinateView sub =
      file.subordinate(std::get<t::SubordinateIndex>(sub_index));
  CHECK(sub.info().name.view() == "Kings Ferry");
  CHECK(sub.info().flags == std::vector<std::string>{"hilo_only"});
  CHECK(sub.reference() == ref);
  CHECK(sub.reference_datum_height() == metres(-1.004));
  CHECK(sub.offsets().time().high == std::chrono::minutes{245});
  CHECK(sub.licence() == ref.licence());
  CHECK(sub.source().id.view() == "noaa-coops");

  CHECK(file.info(sub_index).name.view() == "Kings Ferry");
  CHECK(file.effective_licence(ref_index) == ref.licence());
  CHECK_FALSE(file.find("noaa-coops:8720031"));
  CHECK_FALSE(file.find("8720030"));
  CHECK(file.references().size() == 1);
  CHECK(file.references()[0] == ref);
  CHECK(file.subordinates()[0] == sub);
  CHECK(file.subordinates()[0].index() == sub.index());
  CHECK(file.references()[0].index() == ref.index());

  // DTL has no core VerticalDatum yet; nothing else is worth a warning.
  CHECK(built.warnings ==
        std::vector<HarmonicsWarning>{t::DatumUnsupported{
            .datum = DatumToken::dtl, .stations = 1, .first_station = 1}});
  check_agreement(worked_example());
}

TEST_CASE(
    "a file is a value: equal whatever the input order, shared by "
    "copies",
    "[core][tide][harmonics]") {
  const HarmonicsFile a = build(worked_example()).file;
  HarmonicsInput shuffled = worked_example();
  std::ranges::reverse(shuffled.stations);
  std::ranges::reverse(shuffled.constituents);
  t::SourceInput other = noaa_source();
  other.id = t::SourceToken::make("aaa").value();
  shuffled.sources.insert(shuffled.sources.begin(), other);
  HarmonicsInput with_other = worked_example();
  with_other.sources.push_back(other);
  // The extra source changes the content, so compare like with like.
  CHECK(build(shuffled).file == build(with_other).file);
  CHECK_FALSE(build(shuffled).file == a);
  // A row's index points at the same constituent after reordering.
  const HarmonicsFile b = build(shuffled).file;
  const auto ref = b.references()[0];
  CHECK(b.constituents().name(ref.constants()[0].constituent).view() == "M2");
  CHECK(b.sources()[0].id.view() == "aaa");

  // The copy is the subject: it shares the records.
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
  const HarmonicsFile copy = a;
  CHECK(copy == a);
  CHECK(&copy.header() == &a.header());
  HarmonicsInput changed = worked_example();
  reference_of(changed).constants[0] = row("M2", 0.871, 33.7);
  const HarmonicsFile other_rows = build(std::move(changed)).file;
  CHECK_FALSE(other_rows == a);
}

TEST_CASE("references and subordinates are stored sorted by id",
          "[core][tide][harmonics]") {
  HarmonicsInput in = worked_example();
  for (const std::string_view id :
       {"noaa-coops:9000000", "noaa-coops:1000000", "noaa-coops:5000000"}) {
    auto r = fernandina_beach();
    r.station.id = harmonics_id(id);
    in.stations.emplace_back(std::move(r));
    auto s = kings_ferry();
    s.station.id = harmonics_id(std::string{id} + "-sub");
    in.stations.emplace_back(std::move(s));
  }
  const HarmonicsFile f = build(std::move(in)).file;
  std::vector<std::string> refs;
  for (const auto& r : f.references()) {
    refs.emplace_back(r.info().id.value());
  }
  CHECK(std::ranges::is_sorted(refs));
  CHECK(refs.size() == 4);
  std::vector<std::string> subs;
  for (const auto& s : f.subordinates()) {
    subs.emplace_back(s.info().id.value());
    CHECK(s.reference().info().id.value() == "noaa-coops:8720030");
  }
  CHECK(std::ranges::is_sorted(subs));
  for (const auto& id : refs) {
    CHECK(f.info(must(f.find(id))).id.value() == id);
  }
  for (const auto& id : subs) {
    CHECK(f.info(must(f.find(id))).id.value() == id);
  }
}

TEST_CASE("each rule reports its error and its code",
          "[core][tide][harmonics]") {
  using enum t::HarmonicsErrc;
  expect_error(
      "created is 25:00",
      [](HarmonicsInput& in) { in.header.created.hour = 25; },
      t::InvalidCreated{}, date);
  expect_error(
      "created on 31 February",
      [](HarmonicsInput& in) { in.header.created.date = ymd(2026, 2, 31); },
      t::InvalidCreated{}, date);
  expect_error(
      "created at minute 60",
      [](HarmonicsInput& in) { in.header.created.minute = 60; },
      t::InvalidCreated{}, date);
  expect_error(
      "created at second -1",
      [](HarmonicsInput& in) { in.header.created.second = -1; },
      t::InvalidCreated{}, date);
  expect_error(
      "a source twice",
      [](HarmonicsInput& in) { in.sources.push_back(noaa_source()); },
      t::DuplicateSource{.source = 1, .first = 0}, duplicate_source);
  expect_error(
      "a source licence that does not parse",
      [](HarmonicsInput& in) { in.sources[0].licence = "MIT AND"; },
      t::InvalidSourceLicence{
          .source = 0,
          .error = {.offset = 7, .code = t::LicenceErrc::expected_licence}},
      licence);
  expect_error(
      "retrieved on 30 February",
      [](HarmonicsInput& in) { in.sources[0].retrieved = ymd(2026, 2, 30); },
      t::InvalidRetrieved{.source = 0}, date);
  expect_error(
      "a constituent twice, ignoring case",
      [](HarmonicsInput& in) {
        in.constituents.push_back(compound("m4", 57.9682084, {{"M2", 2}}));
      },
      t::DuplicateName{.entry = 7, .first = 5}, duplicate_constituent);
  expect_error(
      "a compound term that names no entry",
      [](HarmonicsInput& in) {
        in.constituents.push_back(
            compound("MK3", 44.0251729, {{"M2", 1}, {"K2", 1}}));
      },
      t::InvalidTerm{.entry = 7, .error = t::UnknownTerm{.term = name("K2")}},
      compound_term);
  expect_error(
      "a compound term that names a compound",
      [](HarmonicsInput& in) {
        in.constituents.push_back(compound("M8", 115.9364169, {{"M4", 2}}));
      },
      t::InvalidTerm{.entry = 7, .error = t::TermNotDarwin{.term = name("M4")}},
      compound_term);
  expect_error(
      "a speed off by 2e-5",
      [](HarmonicsInput& in) { in.constituents[0].speed += 2e-5; },
      t::SpeedMismatch{.entry = 0, .derived = m2_speed()}, speed_mismatch);
  expect_error(
      "a station in no source's namespace",
      [](HarmonicsInput& in) {
        subordinate_of(in).station.id = harmonics_id("ticon-4:x");
      },
      t::UnknownSource{.station = 0}, unknown_source);
  expect_error(
      "a station id without a namespace",
      [](HarmonicsInput& in) {
        subordinate_of(in).station.id = harmonics_id("8720001");
      },
      t::UnknownSource{.station = 0}, unknown_source);
  expect_error(
      "a station licence that does not parse",
      [](HarmonicsInput& in) { reference_of(in).station.licence = "(MIT"; },
      t::InvalidStationLicence{
          .station = 1,
          .error = {.offset = 4, .code = t::LicenceErrc::unbalanced}},
      licence);
  expect_error(
      "a row naming no entry",
      [](HarmonicsInput& in) {
        reference_of(in).constants.push_back(row("m2", 0.1, 1.0));
      },
      t::UnknownConstituent{.station = 1, .row = 7}, unknown_constituent);
  expect_error(
      "a row twice",
      [](HarmonicsInput& in) {
        reference_of(in).constants.push_back(row("S2", 0.1, 1.0));
      },
      t::DuplicateRow{.station = 1, .row = 7, .first = 1}, duplicate_row);
  expect_error(
      "z0 not finite",
      [](HarmonicsInput& in) {
        reference_of(in).z0 = metres(mov::test::infinity);
      },
      t::InvalidNumber{.station = 1, .number = t::StationNumber::z0}, schema);
  expect_error(
      "record years 0",
      [](HarmonicsInput& in) {
        subordinate_of(in).station.record = t::RecordInput{
            .start = std::nullopt, .end = std::nullopt, .years = 0.0};
      },
      t::InvalidNumber{.station = 0, .number = t::StationNumber::record_years},
      schema);
  expect_error(
      "a computed geodetic datum",
      [](HarmonicsInput& in) {
        reference_of(in).computed_datums =
            computed({{.datum = DatumToken::ngvd29, .height = metres(0.1)}});
      },
      t::UncomputableDatum{.station = 1, .datum = DatumToken::ngvd29}, schema);
  expect_error(
      "a datum both published and computed",
      [](HarmonicsInput& in) {
        reference_of(in).computed_datums =
            computed({{.datum = DatumToken::lat, .height = metres(-1.3)},
                      {.datum = DatumToken::mllw, .height = metres(-1.0)}});
      },
      t::DatumConflict{.station = 1, .datum = DatumToken::mllw},
      datum_conflict);
  expect_error(
      "a record start on 31 April",
      [](HarmonicsInput& in) {
        subordinate_of(in).station.record =
            t::RecordInput{.start = ymd(2001, 4, 31),
                           .end = std::nullopt,
                           .years = std::nullopt};
      },
      t::InvalidStationDate{.station = 0, .date = t::StationDate::record_start},
      date);
  expect_error(
      "a record end on 0 January",
      [](HarmonicsInput& in) {
        subordinate_of(in).station.record =
            t::RecordInput{.start = std::nullopt,
                           .end = ymd(2001, 1, 0),
                           .years = std::nullopt};
      },
      t::InvalidStationDate{.station = 0, .date = t::StationDate::record_end},
      date);
  expect_error(
      "a record ending before it starts",
      [](HarmonicsInput& in) {
        subordinate_of(in).station.record =
            t::RecordInput{.start = ymd(2001, 1, 2),
                           .end = ymd(2001, 1, 1),
                           .years = std::nullopt};
      },
      t::InvertedDates{.station = 0, .range = t::DateRange::record}, date);
  expect_error(
      "a computed period starting on 29 February 2007",
      [](HarmonicsInput& in) {
        reference_of(in).computed_datums =
            computed({{.datum = DatumToken::lat, .height = metres(-1.3)}},
                     ymd(2007, 2, 29));
      },
      t::InvalidStationDate{.station = 1, .date = t::StationDate::period_start},
      date);
  expect_error(
      "a computed period ending on 32 January",
      [](HarmonicsInput& in) {
        reference_of(in).computed_datums =
            computed({{.datum = DatumToken::lat, .height = metres(-1.3)}},
                     ymd(2007, 1, 1), ymd(2026, 1, 32));
      },
      t::InvalidStationDate{.station = 1, .date = t::StationDate::period_end},
      date);
  expect_error(
      "a computed period ending before it starts",
      [](HarmonicsInput& in) {
        reference_of(in).computed_datums =
            computed({{.datum = DatumToken::lat, .height = metres(-1.3)}},
                     ymd(2026, 1, 1), ymd(2007, 1, 1));
      },
      t::InvertedDates{.station = 1, .range = t::DateRange::period}, date);
  expect_error(
      "a computed period of 18 years",
      [](HarmonicsInput& in) {
        reference_of(in).computed_datums =
            computed({{.datum = DatumToken::lat, .height = metres(-1.3)}},
                     ymd(2008, 1, 1), ymd(2026, 1, 1));
      },
      t::ShortDatumPeriod{.station = 1}, datum_conflict);
  expect_error(
      "a station twice",
      [](HarmonicsInput& in) { in.stations.emplace_back(kings_ferry()); },
      t::DuplicateStation{.station = 2, .first = 0}, duplicate_station);
  expect_error(
      "a reference that is not in the file",
      [](HarmonicsInput& in) {
        subordinate_of(in).reference = harmonics_id("noaa-coops:1");
      },
      t::ReferenceMissing{.station = 0}, reference);
  expect_error(
      "a reference that is a subordinate",
      [](HarmonicsInput& in) {
        auto other = kings_ferry();
        other.station.id = harmonics_id("noaa-coops:8720002");
        other.reference = harmonics_id("noaa-coops:8720001");
        in.stations.emplace_back(std::move(other));
      },
      t::ReferenceNotReference{.station = 2, .referenced = 0}, reference);
  expect_error(
      "an offsets datum the reference does not know",
      [](HarmonicsInput& in) {
        subordinate_of(in).offsets =
            t::SubordinateOffsets::make(
                {.minutes = {.high = 0.0, .low = 0.0},
                 .height = t::HeightRatio{.ratio = {.high = 1.0, .low = 1.0}},
                 .datum = t::TideDatum{DatumToken::hat}})
                .value();
      },
      t::OffsetDatumMissing{.station = 0, .datum = DatumToken::hat},
      offset_datum);
}

TEST_CASE("errors come in the pinned order and validate finds them all",
          "[core][tide][harmonics]") {
  HarmonicsInput in = worked_example();
  // Added from the last step to the first, so document order alone would
  // not explain the result.
  in.stations.emplace_back(fernandina_beach());               // 5: duplicate id
  reference_of(in).constants.push_back(row("XX", 0.1, 1.0));  // 4: row
  subordinate_of(in).station.id = harmonics_id("other:8720001");  // 4: source
  in.constituents[3].speed = 0.0;                                 // 3: speed
  in.sources[0].licence = "";                                     // 2: licence
  in.header.created.hour = 24;                                    // 1: created
  const auto report = t::validate(in);
  const std::vector<HarmonicsError> expected{
      t::InvalidCreated{},
      t::InvalidSourceLicence{
          .source = 0, .error = {.offset = 0, .code = t::LicenceErrc::empty}},
      t::SpeedMismatch{.entry = 3, .derived = k1_speed()},
      t::UnknownSource{.station = 0},
      t::UnknownConstituent{.station = 1, .row = 7},
      t::DuplicateStation{.station = 2, .first = 1},
  };
  CHECK(report.errors == expected);
  CHECK(make_error(in) == expected.front());

  // Removing the first error each time, make() reports the next.
  in.header.created.hour = 23;
  CHECK(make_error(in) == expected[1]);
  in.sources[0].licence = "MIT";
  CHECK(make_error(in) == expected[2]);
  in.constituents[3].speed = 15.0410686;
  CHECK(make_error(in) == expected[3]);
}

TEST_CASE("dependent checks are skipped after a failed one",
          "[core][tide][harmonics]") {
  HarmonicsInput in = worked_example();
  // No speed check for an entry whose terms do not resolve.
  in.constituents.push_back(compound("MK3", 1.0, {{"M2", 1}, {"K9", 1}}));
  // No date order with an invalid date; no length with an inverted period.
  reference_of(in).station.record = t::RecordInput{
      .start = ymd(2001, 13, 1), .end = ymd(2000, 1, 1), .years = std::nullopt};
  reference_of(in).computed_datums =
      computed({{.datum = DatumToken::lat, .height = metres(-1.3)}},
               ymd(2026, 1, 1), ymd(2025, 1, 1));
  // No offsets datum for a missing reference.
  subordinate_of(in).reference = harmonics_id("noaa-coops:0");
  const auto report = t::validate(in);
  const std::vector<HarmonicsError> expected{
      t::InvalidTerm{.entry = 7, .error = t::UnknownTerm{.term = name("K9")}},
      t::InvalidStationDate{.station = 1, .date = t::StationDate::record_start},
      t::InvertedDates{.station = 1, .range = t::DateRange::period},
      t::ReferenceMissing{.station = 0},
  };
  CHECK(report.errors == expected);
  check_agreement(in);
}

TEST_CASE("make and validate agree on random combinations of faults",
          "[core][tide][harmonics][property]") {
  using Mutation = void (*)(HarmonicsInput&);
  const std::vector<Mutation> faults{
      [](HarmonicsInput& in) { in.header.created.second = 60; },
      [](HarmonicsInput& in) { in.sources.push_back(noaa_source()); },
      [](HarmonicsInput& in) { in.sources[0].licence = "A B"; },
      [](HarmonicsInput& in) { in.sources[0].retrieved = ymd(2026, 0, 1); },
      [](HarmonicsInput& in) {
        in.constituents.push_back(compound("ms4", 58.9841042, {{"M2", 1}}));
      },
      [](HarmonicsInput& in) {
        in.constituents.push_back(compound("X", 1.0, {{"M4", 1}}));
      },
      [](HarmonicsInput& in) { in.constituents[6].speed = 1.0; },
      [](HarmonicsInput& in) {
        subordinate_of(in).station.id = harmonics_id("x:1");
      },
      [](HarmonicsInput& in) { subordinate_of(in).station.licence = "("; },
      [](HarmonicsInput& in) {
        reference_of(in).constants.push_back(row("Q1", 0.1, 1.0));
      },
      [](HarmonicsInput& in) {
        reference_of(in).constants.push_back(row("M2", 0.1, 1.0));
      },
      [](HarmonicsInput& in) {
        reference_of(in).computed_datums =
            computed({{.datum = DatumToken::mhw, .height = metres(0.1)}},
                     ymd(2010, 1, 1));
      },
      [](HarmonicsInput& in) {
        subordinate_of(in).station.record =
            t::RecordInput{.start = ymd(2002, 1, 1),
                           .end = ymd(2001, 1, 1),
                           .years = std::nullopt};
      },
      [](HarmonicsInput& in) { in.stations.emplace_back(fernandina_beach()); },
      [](HarmonicsInput& in) {
        subordinate_of(in).reference = harmonics_id("noaa-coops:8720001");
      },
  };
  std::seed_seq seed{2026, 10, 15};  // fixed: a failure must reproduce
  std::mt19937 rng{seed};
  std::bernoulli_distribution pick{0.2};
  for (int trial = 0; trial < 500; ++trial) {
    HarmonicsInput in = worked_example();
    std::size_t applied = 0;
    for (const Mutation m : faults) {
      if (pick(rng)) {
        m(in);
        ++applied;
      }
    }
    INFO("trial " << trial << ", faults " << applied);
    check_agreement(in);
    CHECK(t::validate(in).ok() == (applied == 0));
  }
}

TEST_CASE(
    "warnings: redefined, unsupported datum, unused constituent and "
    "source",
    "[core][tide][harmonics]") {
  HarmonicsInput in = worked_example();
  // K1 with another node factor is a redefinition.
  std::get<t::Darwin>(in.constituents[3].entry.definition).f =
      t::NodeFactor::f76;
  // M4 as a Darwin entry with M2's V and u doubled has f = f78, where
  // {M2: 2} has f78^2: the exponents differ, so it is a redefinition.
  in.constituents[5] = darwin("M4", 57.9682084,
                              {.v = {4, -4, 4, 0, 0},
                               .c_deg = 0,
                               .u = {4, -4, 0, 0, 0, 0, 0},
                               .f = t::NodeFactor::f78});
  // An alias spelled with the catalogue definition is not one: LDA2 = LAM2.
  in.constituents.push_back(darwin("LDA2", 29.4556253,
                                   {.v = {2, -1, 0, 1, 0},
                                    .c_deg = 180,
                                    .u = {2, -2, 0, 0, 0, 0, 0},
                                    .f = t::NodeFactor::f78}));
  // A constituent only a compound uses is used; SA is not used at all.
  in.constituents.push_back(darwin("SA", 0.0410686,
                                   {.v = {0, 0, 1, 0, 0},
                                    .c_deg = 0,
                                    .u = {0, 0, 0, 0, 0, 0, 0},
                                    .f = t::NodeFactor::unity}));
  in.constituents.push_back(
      compound("2SM2", 31.0158958, {{"S2", 2}, {"M2", -1}}));
  // S4 as a Darwin entry is the catalogue's {S2: 2}: S2 has no node factor,
  // so the flattened Arguments are equal and it is no redefinition.
  in.constituents.push_back(darwin("S4", 60.0,
                                   {.v = {4, 0, 0, 0, 0},
                                    .c_deg = 0,
                                    .u = {0, 0, 0, 0, 0, 0, 0},
                                    .f = t::NodeFactor::unity}));
  reference_of(in).constants.erase(reference_of(in).constants.begin() + 1);
  reference_of(in).constants.push_back(row("2SM2", 0.01, 1.0));
  reference_of(in).constants.push_back(row("S4", 0.01, 1.0));
  // A computed LAT and HAT add to the DTL station count.
  reference_of(in).computed_datums =
      computed({{.datum = DatumToken::lat, .height = metres(-1.3)},
                {.datum = DatumToken::hat, .height = metres(1.3)}});
  auto second = fernandina_beach();
  second.station.id = harmonics_id("noaa-coops:9000000");
  second.computed_datums =
      computed({{.datum = DatumToken::lat, .height = metres(-1.2)}});
  in.stations.emplace_back(std::move(second));
  in.sources.push_back(source("unused", "MIT"));

  const auto built = build(in);
  const std::vector<HarmonicsWarning> expected{
      t::ConstituentRedefined{.entry = 3},
      t::ConstituentRedefined{.entry = 5},
      t::DatumUnsupported{
          .datum = DatumToken::hat, .stations = 1, .first_station = 1},
      t::DatumUnsupported{
          .datum = DatumToken::dtl, .stations = 2, .first_station = 1},
      t::DatumUnsupported{
          .datum = DatumToken::lat, .stations = 2, .first_station = 1},
      t::UnusedConstituent{.entry = 7},
      t::UnusedConstituent{.entry = 8},
      t::UnusedSource{.source = 1},
  };
  CHECK(built.warnings == expected);
  check_agreement(in);
  CHECK(t::code(expected[0]) == t::HarmonicsWarningCode::constituent_redefined);
  CHECK(t::code(expected[2]) == t::HarmonicsWarningCode::datum_unsupported);
  CHECK(t::code(expected[5]) == t::HarmonicsWarningCode::unused_constituent);
  CHECK(t::code(expected[7]) == t::HarmonicsWarningCode::unused_source);
}

TEST_CASE("validate reports warnings even when it reports errors",
          "[core][tide][harmonics]") {
  HarmonicsInput in = worked_example();
  in.sources.push_back(source("spare", "MIT"));
  in.header.created.hour = 99;
  const auto report = t::validate(in);
  CHECK_FALSE(report.ok());
  CHECK(std::ranges::find(report.warnings,
                          HarmonicsWarning{t::UnusedSource{.source = 1}}) !=
        report.warnings.end());
}

TEST_CASE("licences: a station's overrides its source's",
          "[core][tide][harmonics]") {
  HarmonicsInput in = worked_example();
  subordinate_of(in).station.licence = "CC-BY-NC-4.0";
  const HarmonicsFile f = build(std::move(in)).file;
  const auto sub = f.subordinates()[0];
  CHECK(sub.licence().text() == "CC-BY-NC-4.0");
  CHECK(sub.licence().carries_non_commercial());
  CHECK(f.effective_licence(sub.index()) == sub.licence());
  CHECK(sub.reference().licence().text() ==
        "LicenseRef-US-Government-Public-Domain");
}

TEST_CASE("a subordinate's reference datum height: MSL, published, computed",
          "[core][tide][harmonics]") {
  const auto offsets = [](t::TideDatum d) {
    return t::SubordinateOffsets::make(
               {.minutes = {.high = -17.0, .low = -29.0},
                .height = t::HeightOffset{.offset = {.high = metres(-0.06),
                                                     .low = metres(0.03)}},
                .datum = d})
        .value();
  };
  HarmonicsInput in = worked_example();
  subordinate_of(in).offsets = offsets(t::TideDatum{t::MeanSeaLevel{}});
  const HarmonicsFile at_msl = build(in).file;
  CHECK(at_msl.subordinates()[0].reference_datum_height() == metres(0.0));
  subordinate_of(in).offsets = offsets(t::TideDatum{DatumToken::lat});
  reference_of(in).computed_datums =
      computed({{.datum = DatumToken::lat, .height = metres(-1.25)}});
  const auto f = build(in).file;
  CHECK(f.subordinates()[0].reference_datum_height() == metres(-1.25));
  CHECK(f.references()[0].info().datums.origin(t::TideDatum{DatumToken::lat}) ==
        t::DatumOrigin::computed);
  CHECK(must(f.references()[0].info().datums.computed()).period.end ==
        std::chrono::sys_days{ymd(2026, 1, 1)});
}

TEST_CASE("records keep their dates and years", "[core][tide][harmonics]") {
  HarmonicsInput in = worked_example();
  reference_of(in).station.record = t::RecordInput{
      .start = ymd(2021, 1, 1), .end = std::nullopt, .years = 5.0};
  const auto f = build(std::move(in)).file;
  const t::Record record = must(f.references()[0].info().record);
  CHECK(record.dates.first() == std::chrono::sys_days{ymd(2021, 1, 1)});
  CHECK_FALSE(record.dates.last());
  CHECK(record.years == 5.0);
  CHECK_FALSE(f.subordinates()[0].info().record);
}

TEST_CASE("every error and warning code has its format spelling",
          "[core][tide][harmonics]") {
  using enum t::HarmonicsErrc;
  const std::vector<std::pair<t::HarmonicsErrc, std::string_view>> errors{
      {schema, "E-SCHEMA"},
      {date, "E-DATE"},
      {duplicate_source, "E-DUPLICATE-SOURCE"},
      {licence, "E-LICENCE"},
      {duplicate_constituent, "E-DUPLICATE-CONSTITUENT"},
      {compound_term, "E-COMPOUND-TERM"},
      {speed_mismatch, "E-SPEED-MISMATCH"},
      {unknown_source, "E-UNKNOWN-SOURCE"},
      {unknown_constituent, "E-UNKNOWN-CONSTITUENT"},
      {duplicate_row, "E-DUPLICATE-ROW"},
      {datum_conflict, "E-DATUM-CONFLICT"},
      {duplicate_station, "E-DUPLICATE-STATION"},
      {reference, "E-REFERENCE"},
      {offset_datum, "E-OFFSET-DATUM"},
  };
  for (const auto& [c, text] : errors) {
    CHECK(t::to_string(c) == text);
  }
  using W = t::HarmonicsWarningCode;
  CHECK(t::to_string(W::constituent_redefined) == "W-CONSTITUENT-REDEFINED");
  CHECK(t::to_string(W::unused_constituent) == "W-UNUSED-CONSTITUENT");
  CHECK(t::to_string(W::unused_source) == "W-UNUSED-SOURCE");
  CHECK(t::to_string(W::datum_unsupported) == "W-DATUM-UNSUPPORTED");
}
