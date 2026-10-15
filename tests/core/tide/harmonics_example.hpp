// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The worked example of docs/harmonics-json.md section 13.1 as decoded
// input (what the io reader of WP H5 will produce from that file), and small
// builders for variations of it.

#pragma once

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/geo.hpp"
#include "mov/core/station.hpp"
#include "mov/core/tide/constituent.hpp"
#include "mov/core/tide/constituent_set.hpp"
#include "mov/core/tide/datums.hpp"
#include "mov/core/tide/harmonics_input.hpp"
#include "mov/core/tide/node_factor.hpp"
#include "mov/core/tide/source.hpp"
#include "mov/core/tide/station_info.hpp"
#include "mov/core/units.hpp"

namespace mov::test::tide {

namespace core = mov::core;
namespace t = mov::core::tide;

/// The value of an optional the test needs; an empty one fails the test.
template <class T>
T must(std::optional<T> o) {
  if (not o) {
    FAIL("an expected value is missing");
  }
  return *std::move(o);
}

inline core::Length metres(double m) {
  return core::Length::in(m, core::LengthUnit::meter);
}

inline std::chrono::year_month_day ymd(int y, unsigned m, unsigned d) {
  return std::chrono::year{y} / std::chrono::month{m} / std::chrono::day{d};
}

inline core::StationId<core::provider::Harmonics> harmonics_id(
    std::string_view id) {
  return core::StationId<core::provider::Harmonics>::make(id).value();
}

inline t::ConstituentName name(std::string_view n) {
  return t::ConstituentName::make(n).value();
}

inline t::ConstituentInput darwin(std::string_view n, double speed,
                                  t::Darwin d) {
  return t::ConstituentInput{
      .entry =
          t::DictionaryEntry{
              .name = name(n), .definition = d, .description = std::nullopt},
      .speed = speed};
}

inline t::ConstituentInput compound(
    std::string_view n, double speed,
    std::initializer_list<std::pair<std::string_view, int>> terms) {
  std::vector<t::CompoundTerms::Term> parsed;
  for (const auto& [base, coefficient] : terms) {
    parsed.push_back(t::CompoundTerms::Term{
        .base = name(base),
        .coefficient = static_cast<std::int8_t>(coefficient)});
  }
  return t::ConstituentInput{
      .entry = t::DictionaryEntry{.name = name(n),
                                  .definition =
                                      t::CompoundTerms::make(parsed).value(),
                                  .description = std::nullopt},
      .speed = speed};
}

inline t::DatumHeights heights(std::initializer_list<t::TokenHeight> rows) {
  return t::DatumHeights::make(std::span{rows.begin(), rows.size()}).value();
}

inline t::ConstantInput row(std::string_view n, double h, double g) {
  return t::ConstantInput{
      .constituent = name(n),
      .value = t::HarmonicConstant::make(
                   {.amplitude_m = h, .phase_deg = g, .sd = std::nullopt})
                   .value()};
}

inline t::StationInput station(std::string_view id, std::string_view n,
                               double lat, double lon) {
  return t::StationInput{
      .id = harmonics_id(id),
      .name = core::StationText::make(std::string{n}).value(),
      .location = core::Location::make({.lat = lat, .lon = lon}).value(),
      .licence = std::nullopt,
      .country = std::nullopt,
      .time_zone = std::nullopt,
      .datums = {},
      .datum_epoch = std::nullopt,
      .record = std::nullopt,
      .flags = {},
      .provenance = {}};
}

inline t::SourceInput noaa_source() {
  return t::SourceInput{
      .id = t::SourceToken::make("noaa-coops").value(),
      .name = core::StationText::make("NOAA CO-OPS Metadata API").value(),
      .url = "https://api.tidesandcurrents.noaa.gov/mdapi/prod/",
      .licence = "LicenseRef-US-Government-Public-Domain",
      .attribution =
          "NOAA National Ocean Service, Center for Operational Oceanographic "
          "Products and Services",
      .retrieved = ymd(2026, 10, 8),
      .version = std::nullopt,
      .sha256 = std::nullopt,
      .notes = std::nullopt};
}

/// The seven constituents of the example, in its order.
inline std::vector<t::ConstituentInput> example_constituents() {
  using t::NodeFactor;
  return {
      darwin("M2", 28.9841042,
             {.v = {2, -2, 2, 0, 0},
              .c_deg = 0,
              .u = {2, -2, 0, 0, 0, 0, 0},
              .f = NodeFactor::f78}),
      darwin("S2", 30.0,
             {.v = {2, 0, 0, 0, 0},
              .c_deg = 0,
              .u = {0, 0, 0, 0, 0, 0, 0},
              .f = NodeFactor::unity}),
      darwin("N2", 28.4397295,
             {.v = {2, -3, 2, 1, 0},
              .c_deg = 0,
              .u = {2, -2, 0, 0, 0, 0, 0},
              .f = NodeFactor::f78}),
      darwin("K1", 15.0410686,
             {.v = {1, 0, 1, 0, 0},
              .c_deg = -90,
              .u = {0, 0, -1, 0, 0, 0, 0},
              .f = NodeFactor::f227}),
      darwin("O1", 13.9430356,
             {.v = {1, -2, 1, 0, 0},
              .c_deg = 90,
              .u = {2, -1, 0, 0, 0, 0, 0},
              .f = NodeFactor::f75}),
      compound("M4", 57.9682084, {{"M2", 2}}),
      compound("MS4", 58.9841042, {{"M2", 1}, {"S2", 1}}),
  };
}

/// Kings Ferry, subordinate of 8720030 (stations[0] of the example).
inline t::SubordinateInput kings_ferry() {
  t::StationInput s =
      station("noaa-coops:8720001", "Kings Ferry", 30.7867, -81.84);
  s.country = "USA";
  s.flags = {"hilo_only"};
  s.provenance.source_record = "8720001";
  return t::SubordinateInput{
      .station = std::move(s),
      .reference = harmonics_id("noaa-coops:8720030"),
      .offsets =
          t::SubordinateOffsets::make(
              {.minutes = {.high = 245.0, .low = 249.0},
               .height = t::HeightRatio{.ratio = {.high = 0.49, .low = 1.16}},
               .datum = t::TideDatum{t::DatumToken::mllw}})
              .value()};
}

/// Fernandina Beach, the reference (stations[1] of the example).
inline t::ReferenceInput fernandina_beach() {
  using enum t::DatumToken;
  t::StationInput s = station("noaa-coops:8720030", "Fernandina Beach, FL",
                              30.671356, -81.46584);
  s.country = "USA";
  s.time_zone = "America/New_York";
  s.datums = heights({{.datum = mhhw, .height = metres(0.995)},
                      {.datum = mhw, .height = metres(0.888)},
                      {.datum = dtl, .height = metres(-0.005)},
                      {.datum = mtl, .height = metres(-0.029)},
                      {.datum = mlw, .height = metres(-0.947)},
                      {.datum = mllw, .height = metres(-1.004)},
                      {.datum = navd88, .height = metres(0.161)},
                      {.datum = stnd, .height = metres(-1.522)}});
  s.datum_epoch = "1983-2001";
  s.provenance.source_record = "8720030";
  s.provenance.notes = "7 of 37 published constituents, for illustration";
  return t::ReferenceInput{
      .station = std::move(s),
      .constants = {row("M2", 0.87, 33.7), row("S2", 0.134, 62.1),
                    row("N2", 0.196, 17.8), row("K1", 0.103, 209.1),
                    row("O1", 0.077, 214.4), row("M4", 0.03, 203.7),
                    row("MS4", 0.016, 230.7)},
      .z0 = metres(0.0),
      .analysis = t::Analysis::nos,
      .computed_datums = std::nullopt};
}

/// docs/harmonics-json.md 13.1, decoded.
inline t::HarmonicsInput worked_example() {
  return t::HarmonicsInput{
      .header =
          t::HeaderInput{
              .version = {.major_version = 1,
                          .minor_version = 0,
                          .patch_version = 0},
              .created = {.date = ymd(2026, 10, 8),
                          .hour = 12,
                          .minute = 0,
                          .second = 0},
              .generator = {.name = "mov-harmonics-builder",
                            .version = "5.0.0-dev",
                            .command = std::nullopt},
              .title = "Worked example (docs/harmonics-json.md section 13)",
              .description = std::nullopt,
              .licence_summary = "NOAA CO-OPS data: U.S. Government work, "
                                 "public domain. Not for navigation."},
      .sources = {noaa_source()},
      .constituents = example_constituents(),
      .stations = {kings_ferry(), fernandina_beach()}};
}

/// The example's reference (stations[1]).
inline t::ReferenceInput& reference_of(t::HarmonicsInput& in) {
  return std::get<t::ReferenceInput>(in.stations[1]);
}

/// The example's subordinate (stations[0]).
inline t::SubordinateInput& subordinate_of(t::HarmonicsInput& in) {
  return std::get<t::SubordinateInput>(in.stations[0]);
}

}  // namespace mov::test::tide
