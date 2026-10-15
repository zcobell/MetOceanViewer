// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// HarmonicsFile::make and validate (WP H2): the worked example of
// docs/harmonics-json.md 13.1 resolved into the file model, every error and
// warning of docs/harmonics-json.md 10 that the model reports, their pinned
// order (docs/harmonics-engine.md 2.7), and the views.

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <random>
#include <ranges>
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
#include "mov/core/tide/year_arguments.hpp"

namespace t = mov::core::tide;
using mov::core::VerticalDatum;
using mov::test::tide::compound;
using mov::test::tide::computed_heights;
using mov::test::tide::darwin;
using mov::test::tide::fernandina_beach;
using mov::test::tide::harmonics_id;
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
using t::ComputableDatum;
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

t::HarmonicsBuild build(HarmonicsInput in) {
  auto built = HarmonicsFile::make(std::move(in));
  REQUIRE(built.has_value());
  return *std::move(built);
}

double speed_of(const t::Darwin& d) {
  return t::Argument::darwin(d).speed_deg_per_hour();
}

constexpr t::Darwin o1{.v = {1, -2, 1, 0, 0},
                       .c_deg = 90,
                       .u = {2, -1, 0, 0, 0, 0, 0},
                       .f = t::NodeFactor::f75};

/// The single error a fault of the example gives: validate() finds only
/// it, make() returns it, and code() names it.
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

t::ComputedDatumsInput computed(
    std::initializer_list<t::HeightRow<ComputableDatum>> rows,
    std::chrono::year_month_day start = ymd(2007, 1, 1),
    std::chrono::year_month_day end = ymd(2026, 1, 1)) {
  return t::ComputedDatumsInput{
      .start = start, .end = end, .heights = computed_heights(rows)};
}

t::SourceInput source(std::string_view id, std::string licence) {
  t::SourceInput s = noaa_source();
  s.id = t::SourceToken::make(id).value();
  s.licence = std::move(licence);
  return s;
}

t::SubordinateOffsets offsets_to(VerticalDatum d) {
  return t::SubordinateOffsets::make(
             {.minutes = {.high = -17.0, .low = -29.0},
              .height = t::HeightOffset{.offset = {.high = metres(-0.06),
                                                   .low = metres(0.03)}},
              .datum = d})
      .value();
}

}  // namespace

TEST_CASE("the worked example resolves into the file model",
          "[core][tide][harmonics]") {
  const auto built = build(worked_example());
  const HarmonicsFile& file = built.file;
  CHECK(built.warnings.empty());

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
  CHECK(ref.info().datums.height(VerticalDatum::mllw) == metres(-1.004));
  CHECK(ref.info().datums.table().height_above_msl(VerticalDatum::dtl) ==
        metres(-0.005));
  CHECK(ref.z0() == metres(0.0));
  CHECK(ref.analysis() == t::Analysis::nos);
  CHECK(&ref.constituents() == &file.constituents());
  CHECK(ref.licence().text() == "LicenseRef-US-Government-Public-Domain");
  CHECK(ref.source().id.view() == "noaa-coops");
  CHECK(&ref.source() == &file.source(ref.info().source));

  // The constants are a map sorted by constituent index, whatever the rows'
  // order in the file.
  const auto constants = ref.constants();
  REQUIRE(constants.size() == 7);
  CHECK(std::ranges::is_sorted(constants, {}, &t::Constant::constituent));
  const t::ConstituentIndex m2 = must(file.constituents().find("M2"));
  CHECK(must(ref.constant(m2)).amplitude() == metres(0.87));
  CHECK(must(ref.constant(m2)).phase().degrees() == 33.7);
  const t::ConstituentIndex ms4 = must(file.constituents().find("MS4"));
  CHECK(must(ref.constant(ms4)).amplitude() == metres(0.016));

  const t::StationIndex sub_index = must(file.find("noaa-coops:8720001"));
  REQUIRE(std::holds_alternative<t::SubordinateIndex>(sub_index));
  const t::SubordinateView sub =
      file.subordinate(std::get<t::SubordinateIndex>(sub_index));
  CHECK(sub.info().name.view() == "Kings Ferry");
  CHECK(sub.info().flags == std::vector<std::string>{"hilo_only"});
  CHECK(sub.reference() == ref);
  CHECK(sub.reference_datum_height() == metres(-1.004));
  CHECK(sub.offsets().time().high == std::chrono::minutes{245});
  CHECK(sub.offsets().datum() == VerticalDatum::mllw);
  CHECK(sub.licence() == ref.licence());
  CHECK(sub.source().id.view() == "noaa-coops");

  CHECK(file.info(sub_index).name.view() == "Kings Ferry");
  CHECK(file.effective_licence(ref_index) == ref.licence());
  CHECK_FALSE(file.find("noaa-coops:8720031"));
  CHECK_FALSE(file.find("8720030"));
  CHECK(std::ranges::distance(file.references()) == 1);
  CHECK(*file.references().begin() == ref);
  CHECK(*file.subordinates().begin() == sub);
  CHECK((*file.references().begin()).index() == ref.index());
  CHECK((*file.subordinates().begin()).index() == sub.index());
}

TEST_CASE("a station without constants is z0 alone (the reader requires rows)",
          "[core][tide][harmonics]") {
  HarmonicsInput in = worked_example();
  reference_of(in).constants.clear();
  // M2 only: the others would be unused anyway.
  in.constituents.erase(in.constituents.begin() + 1, in.constituents.end());
  const auto built = build(std::move(in));
  CHECK((*built.file.references().begin()).constants().empty());
  CHECK(built.warnings ==
        std::vector<HarmonicsWarning>{t::UnusedConstituent{.entry = 0}});
}

TEST_CASE(
    "a file is a value: equal whatever the input order, shared by "
    "copies, whole after a move",
    "[core][tide][harmonics]") {
  const HarmonicsFile a = build(worked_example()).file;
  HarmonicsInput shuffled = worked_example();
  std::ranges::reverse(shuffled.stations);
  std::ranges::reverse(shuffled.constituents);
  std::ranges::reverse(
      std::get<t::ReferenceInput>(shuffled.stations[0]).constants);
  const t::SourceInput other = source("aaa", "MIT");
  shuffled.sources.insert(shuffled.sources.begin(), other);
  HarmonicsInput with_other = worked_example();
  with_other.sources.push_back(other);
  // The extra source changes the content, so compare like with like.
  const HarmonicsFile b = build(std::move(shuffled)).file;
  const HarmonicsFile c = build(std::move(with_other)).file;
  CHECK(b == c);
  CHECK_FALSE(b == a);
  CHECK(b.sources()[0].id.view() == "aaa");
  const auto ref = *b.references().begin();
  CHECK(must(ref.constant(must(b.constituents().find("M2")))).amplitude() ==
        metres(0.87));

  // The copy is the subject: it shares the records.
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
  const HarmonicsFile copy = a;
  CHECK(copy == a);
  CHECK(&copy.header() == &a.header());
  HarmonicsFile source_of_move = a;
  // The move is the subject: HarmonicsFile declares no move constructor.
  // NOLINTNEXTLINE(performance-move-const-arg)
  const HarmonicsFile moved = std::move(source_of_move);
  // A move is a copy: the moved-from file is still the same file.
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  CHECK(source_of_move == moved);

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
  for (const auto r : f.references()) {
    refs.emplace_back(r.info().id.value());
  }
  CHECK(std::ranges::is_sorted(refs));
  CHECK(refs.size() == 4);
  std::vector<std::string> subs;
  for (const auto s : f.subordinates()) {
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
      [](HarmonicsInput& in) { in.constituents[4].speed += 2e-5; },
      t::SpeedMismatch{.entry = 4, .derived = speed_of(o1)}, speed_mismatch);
  expect_error(
      "a station in no source's namespace",
      [](HarmonicsInput& in) {
        subordinate_of(in).station.id = harmonics_id("ticon-4:x");
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
      "a datum both published and computed",
      [](HarmonicsInput& in) {
        reference_of(in).computed_datums = computed(
            {{.datum = ComputableDatum::lat, .height = metres(-1.3)},
             {.datum = ComputableDatum::mllw, .height = metres(-1.0)}});
      },
      t::DatumConflict{.station = 1, .datum = VerticalDatum::mllw},
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
            computed({{.datum = ComputableDatum::lat, .height = metres(-1.3)}},
                     ymd(2007, 2, 29));
      },
      t::InvalidStationDate{.station = 1, .date = t::StationDate::period_start},
      date);
  expect_error(
      "a computed period ending on 32 January",
      [](HarmonicsInput& in) {
        reference_of(in).computed_datums =
            computed({{.datum = ComputableDatum::lat, .height = metres(-1.3)}},
                     ymd(2007, 1, 1), ymd(2026, 1, 32));
      },
      t::InvalidStationDate{.station = 1, .date = t::StationDate::period_end},
      date);
  expect_error(
      "a computed period ending before it starts",
      [](HarmonicsInput& in) {
        reference_of(in).computed_datums =
            computed({{.datum = ComputableDatum::lat, .height = metres(-1.3)}},
                     ymd(2026, 1, 1), ymd(2007, 1, 1));
      },
      t::InvertedDates{.station = 1, .range = t::DateRange::period}, date);
  expect_error(
      "a computed period of 18 years",
      [](HarmonicsInput& in) {
        reference_of(in).computed_datums =
            computed({{.datum = ComputableDatum::lat, .height = metres(-1.3)}},
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
        subordinate_of(in).offsets = offsets_to(VerticalDatum::hat);
      },
      t::OffsetDatumMissing{.station = 0, .datum = VerticalDatum::hat},
      offset_datum);
}

namespace {

/// A fault of the base input, the one error it gives alone, and where that
/// error stands in the pinned order: (step, item, check, row).
struct Fault {
  std::array<std::size_t, 4> key;
  void (*mutate)(HarmonicsInput&);
  HarmonicsError error;
};

/// The worked example with a subordinate record and computed datums at the
/// reference, so that each check has something to fail.
HarmonicsInput fault_base() {
  HarmonicsInput in = worked_example();
  subordinate_of(in).station.record = t::RecordInput{
      .start = ymd(2001, 1, 1), .end = ymd(2002, 1, 1), .years = 1.0};
  reference_of(in).computed_datums =
      computed({{.datum = ComputableDatum::lat, .height = metres(-1.3)},
                {.datum = ComputableDatum::hat, .height = metres(1.3)}});
  return in;
}

// Fault positions in the list below that dependent skips name.
constexpr std::size_t short_period = 14;
constexpr std::size_t conflict = 15;
constexpr std::size_t missing_reference = 17;
constexpr std::size_t offset_datum = 18;
constexpr std::array reference_faults{std::size_t{11}, std::size_t{12},
                                      std::size_t{13}, short_period, conflict};

std::vector<Fault> faults() {
  using enum VerticalDatum;
  return {
      {.key = {1, 0, 0, 0},
       .mutate = [](HarmonicsInput& in) { in.header.created.hour = 25; },
       .error = t::InvalidCreated{}},
      {.key = {2, 0, 1, 0},
       .mutate = [](HarmonicsInput& in) { in.sources[0].licence = "MIT AND"; },
       .error =
           t::InvalidSourceLicence{
               .source = 0,
               .error = {.offset = 7,
                         .code = t::LicenceErrc::expected_licence}}},
      {.key = {2, 0, 2, 0},
       .mutate =
           [](HarmonicsInput& in) {
             in.sources[0].retrieved = ymd(2026, 2, 30);
           },
       .error = t::InvalidRetrieved{.source = 0}},
      {.key = {2, 1, 0, 0},
       .mutate =
           [](HarmonicsInput& in) { in.sources.push_back(noaa_source()); },
       .error = t::DuplicateSource{.source = 1, .first = 0}},
      {.key = {3, 4, 2, 0},
       .mutate = [](HarmonicsInput& in) { in.constituents[4].speed += 1.0; },
       .error = t::SpeedMismatch{.entry = 4, .derived = speed_of(o1)}},
      {.key = {3, 6, 1, 0},
       .mutate =
           [](HarmonicsInput& in) {
             in.constituents[6] =
                 compound("MS4", 58.9841042, {{"M2", 1}, {"K9", 1}});
           },
       .error = t::InvalidTerm{.entry = 6,
                               .error = t::UnknownTerm{.term = name("K9")}}},
      {.key = {3, 7, 0, 0},
       .mutate =
           [](HarmonicsInput& in) {
             in.constituents.push_back(compound("m4", 57.9682084, {{"M2", 2}}));
           },
       .error = t::DuplicateName{.entry = 7, .first = 5}},
      {.key = {4, 0, 0, 0},
       .mutate =
           [](HarmonicsInput& in) {
             subordinate_of(in).station.id = harmonics_id("other:8720001");
           },
       .error = t::UnknownSource{.station = 0}},
      {.key = {4, 0, 1, 0},
       .mutate =
           [](HarmonicsInput& in) { subordinate_of(in).station.licence = "("; },
       .error =
           t::InvalidStationLicence{
               .station = 0,
               .error = {.offset = 1,
                         .code = t::LicenceErrc::expected_licence}}},
      {.key = {4, 0, 4, 0},
       .mutate =
           [](HarmonicsInput& in) {
             subordinate_of(in).station.record->years = 0.0;
           },
       .error = t::InvalidNumber{.station = 0,
                                 .number = t::StationNumber::record_years}},
      {.key = {4, 0, 5, 0},
       .mutate =
           [](HarmonicsInput& in) {
             subordinate_of(in).station.record->end = ymd(2000, 1, 1);
           },
       .error = t::InvertedDates{.station = 0, .range = t::DateRange::record}},
      {.key = {4, 1, 2, 6},
       .mutate =
           [](HarmonicsInput& in) {
             reference_of(in).constants[6] = row("Q1", 0.1, 1.0);
           },
       .error = t::UnknownConstituent{.station = 1, .row = 6}},
      {.key = {4, 1, 2, 5},
       .mutate =
           [](HarmonicsInput& in) {
             reference_of(in).constants[5] = row("M2", 0.1, 1.0);
           },
       .error = t::DuplicateRow{.station = 1, .row = 5, .first = 0}},
      {.key = {4, 1, 3, 0},
       .mutate =
           [](HarmonicsInput& in) {
             reference_of(in).z0 = metres(mov::test::infinity);
           },
       .error = t::InvalidNumber{.station = 1, .number = t::StationNumber::z0}},
      {.key = {4, 1, 6, 0},
       .mutate =
           [](HarmonicsInput& in) {
             reference_of(in).computed_datums->start = ymd(2010, 1, 1);
           },
       .error = t::ShortDatumPeriod{.station = 1}},
      {.key = {4, 1, 7, 0},
       .mutate =
           [](HarmonicsInput& in) {
             auto& c = *reference_of(in).computed_datums;
             c.heights = computed_heights(
                 {{.datum = ComputableDatum::lat, .height = metres(-1.3)},
                  {.datum = ComputableDatum::mllw, .height = metres(-1.0)}});
           },
       .error = t::DatumConflict{.station = 1, .datum = mllw}},
      {.key = {5, 0, 2, 0},
       .mutate =
           [](HarmonicsInput& in) {
             in.stations.emplace_back(fernandina_beach());
           },
       .error = t::DuplicateStation{.station = 2, .first = 1}},
      {.key = {5, 1, 0, 0},
       .mutate =
           [](HarmonicsInput& in) {
             subordinate_of(in).reference = harmonics_id("noaa-coops:1");
           },
       .error = t::ReferenceMissing{.station = 0}},
      {.key = {5, 1, 1, 0},
       .mutate =
           [](HarmonicsInput& in) {
             subordinate_of(in).offsets = offsets_to(VerticalDatum::igld85);
           },
       .error = t::OffsetDatumMissing{.station = 0, .datum = igld85}},
  };
}

/// validate()'s errors for the faults in `chosen`, predicted from the
/// single-fault errors: merged in the pinned order, minus the documented
/// dependent skips (conflicts need a valid period; the offsets datum needs
/// a found reference whose own checks passed).
std::vector<HarmonicsError> predicted(const std::vector<Fault>& all,
                                      const std::vector<bool>& chosen) {
  const auto has = [&](std::size_t f) { return chosen[f]; };
  std::vector<const Fault*> applied;
  for (std::size_t f = 0; f < all.size(); ++f) {
    if (not has(f)) {
      continue;
    }
    if (f == conflict and has(short_period)) {
      continue;
    }
    if (f == offset_datum and (has(missing_reference) or
                               std::ranges::any_of(reference_faults, has))) {
      continue;
    }
    applied.push_back(&all[f]);
  }
  std::ranges::sort(applied, {}, &Fault::key);
  std::vector<HarmonicsError> out;
  out.reserve(applied.size());
  for (const Fault* f : applied) {
    out.push_back(f->error);
  }
  return out;
}

void check_faults(const std::vector<Fault>& all,
                  const std::vector<bool>& chosen) {
  HarmonicsInput in = fault_base();
  for (std::size_t f = 0; f < all.size(); ++f) {
    if (chosen[f]) {
      all[f].mutate(in);
    }
  }
  const auto report = t::validate(in);
  const auto expected = predicted(all, chosen);
  CHECK(report.errors == expected);
  auto built = HarmonicsFile::make(std::move(in));
  REQUIRE(built.has_value() == expected.empty());
  if (not built) {
    CHECK(built.error() == expected.front());
  }
}

}  // namespace

TEST_CASE("combined faults: the single-fault errors in the pinned order",
          "[core][tide][harmonics][property]") {
  const std::vector<Fault> all = faults();
  REQUIRE(build(fault_base()).warnings.empty());
  // Each alone, and all together.
  for (std::size_t f = 0; f < all.size(); ++f) {
    INFO("fault " << f);
    std::vector<bool> one(all.size(), false);
    one[f] = true;
    check_faults(all, one);
  }
  check_faults(all, std::vector<bool>(all.size(), true));
  // Random combinations (fixed seed: a failure must reproduce).
  std::seed_seq seed{2026, 10, 15};
  std::mt19937 rng{seed};
  std::bernoulli_distribution pick{0.3};
  for (int trial = 0; trial < 400; ++trial) {
    std::vector<bool> chosen(all.size());
    std::ranges::generate(chosen, [&] { return pick(rng); });
    INFO("trial " << trial);
    check_faults(all, chosen);
  }
}

TEST_CASE("warnings: redefined, unused constituent and source",
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
  in.sources.push_back(source("unused", "MIT"));

  const auto built = build(in);
  const std::vector<HarmonicsWarning> expected{
      t::ConstituentRedefined{.entry = 3}, t::ConstituentRedefined{.entry = 5},
      t::UnusedConstituent{.entry = 7},    t::UnusedConstituent{.entry = 8},
      t::UnusedSource{.source = 1},
  };
  CHECK(built.warnings == expected);
  CHECK(t::validate(in).warnings == expected);
  CHECK(t::code(expected[0]) == t::HarmonicsWarningCode::constituent_redefined);
  CHECK(t::code(expected[2]) == t::HarmonicsWarningCode::unused_constituent);
  CHECK(t::code(expected[4]) == t::HarmonicsWarningCode::unused_source);
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
  const auto sub = *f.subordinates().begin();
  CHECK(sub.licence().text() == "CC-BY-NC-4.0");
  CHECK(sub.licence().carries_non_commercial());
  CHECK(f.effective_licence(sub.index()) == sub.licence());
  CHECK(sub.reference().licence().text() ==
        "LicenseRef-US-Government-Public-Domain");
}

TEST_CASE("a subordinate's reference datum height: MSL, published, computed",
          "[core][tide][harmonics]") {
  HarmonicsInput in = worked_example();
  subordinate_of(in).offsets = offsets_to(VerticalDatum::msl);
  const HarmonicsFile at_msl = build(in).file;
  CHECK((*at_msl.subordinates().begin()).reference_datum_height() ==
        metres(0.0));
  subordinate_of(in).offsets = offsets_to(VerticalDatum::lat);
  reference_of(in).computed_datums =
      computed({{.datum = ComputableDatum::lat, .height = metres(-1.25)}});
  const auto f = build(in).file;
  CHECK((*f.subordinates().begin()).reference_datum_height() == metres(-1.25));
  const auto& datums = (*f.references().begin()).info().datums;
  CHECK(datums.origin(VerticalDatum::lat) == t::DatumOrigin::computed);
  CHECK(datums.table().height_above_msl(VerticalDatum::lat) == metres(-1.25));
  CHECK(must(datums.computed()).period.end() ==
        std::chrono::sys_days{ymd(2026, 1, 1)});
}

TEST_CASE("records keep their dates and years", "[core][tide][harmonics]") {
  HarmonicsInput in = worked_example();
  reference_of(in).station.record = t::RecordInput{
      .start = ymd(2021, 1, 1), .end = std::nullopt, .years = 5.0};
  const auto f = build(std::move(in)).file;
  const t::Record record = must((*f.references().begin()).info().record);
  CHECK(record.dates.first() == std::chrono::sys_days{ymd(2021, 1, 1)});
  CHECK_FALSE(record.dates.last());
  CHECK(record.years == 5.0);
  CHECK_FALSE((*f.subordinates().begin()).info().record);
}

TEST_CASE("year tables from the file belong to its dictionary",
          "[core][tide][harmonics][year_arguments]") {
  const HarmonicsFile f = build(worked_example()).file;
  const auto from_file = must(t::year_arguments(f, std::chrono::year{2026}));
  const auto ref = *f.references().begin();
  const auto from_view = must(t::year_arguments(ref, std::chrono::year{2026}));
  CHECK(from_file == from_view);
  CHECK(from_file.made_for(f.constituents()));
  CHECK(from_file.size() == f.constituents().size());
  const HarmonicsFile other = build(worked_example()).file;
  CHECK_FALSE(from_file.made_for(other.constituents()));
  CHECK(t::year_arguments(f, std::chrono::year{2201}).error() ==
        t::YearOutOfRange{.year = std::chrono::year{2201}});
}
