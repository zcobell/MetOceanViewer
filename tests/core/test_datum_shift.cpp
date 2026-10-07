// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <expected>
#include <initializer_list>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "datum_fixture.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/datum_shift.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"
#include "series_ops_helpers.hpp"
#include "test_helpers.hpp"

using mov::core::DatumHeight;
using mov::core::DatumTable;
using mov::core::Dry;
using mov::core::GenericQuantity;
using mov::core::LengthUnit;
using mov::core::Missing;
using mov::core::MissingOffset;
using mov::core::NotALengthSeries;
using mov::core::Quantity;
using mov::core::Sample;
using mov::core::SeriesMeta;
using mov::core::shift;
using mov::core::ShiftError;
using mov::core::SpeedUnit;
using mov::core::TemperatureUnit;
using mov::core::TimeSeries;
using mov::core::to_string;
using mov::core::Unit;
using mov::core::UnknownSourceDatum;
using mov::core::VerticalDatum;
using mov::test::axis_of;
using mov::test::in_metres;
using mov::test::level_meta;
using mov::test::make_series;
using mov::test::metres;
using mov::test::near_abs;
using mov::test::read_offsets_fixture;
using mov::test::val;

namespace {

const Unit metre = LengthUnit::meter;
const Unit foot = LengthUnit::foot;

DatumTable table_of(VerticalDatum reference,
                    std::initializer_list<DatumHeight> rows) {
  const std::vector<DatumHeight> heights{rows};
  const auto table = DatumTable::from_heights(reference, heights);
  REQUIRE(table.has_value());
  return table.value_or(DatumTable{});
}

// The NOAA 8518750 (The Battery) heights above MSL, from the F12 fixture.
DatumTable battery() {
  const auto stations = read_offsets_fixture();
  const auto& s = stations.at("noaa:8518750");
  const auto table = DatumTable::from_heights(s.reference, s.heights);
  REQUIRE(table.has_value());
  return table.value_or(DatumTable{});
}

std::vector<Sample> samples_of(const TimeSeries& s) {
  return {s.samples().begin(), s.samples().end()};
}

double only(const Sample& s) { return s.value().value_or(-1e99); }

}  // namespace

// ---- offset sign, against NOAA and XTide rows
// ----------------------------------

TEST_CASE("a water level at MSL is above MLLW: the offset is added",
          "[core][datum_shift][fixture]") {
  // 8518750: MLLW is 0.77527 m below MSL, so a level of 0 above MSL is
  // 0.77527 m above MLLW (the legacy column +0.77527).
  const TimeSeries at_msl = make_series(axis_of({0}), {val(0.0)},
                                        level_meta(metre, VerticalDatum::msl));
  const auto in_mllw = shift(at_msl, VerticalDatum::mllw, battery());
  REQUIRE(in_mllw.has_value());
  CHECK(near_abs(only(in_mllw->samples()[0]), 0.77527, 1e-12));
  CHECK(in_mllw->meta().datum() == VerticalDatum::mllw);

  // The other way: 0 above MLLW is 0.77527 m BELOW MSL.
  const TimeSeries at_mllw = make_series(
      axis_of({0}), {val(0.0)}, level_meta(metre, VerticalDatum::mllw));
  const auto in_msl = shift(at_mllw, VerticalDatum::msl, battery());
  REQUIRE(in_msl.has_value());
  CHECK(near_abs(only(in_msl->samples()[0]), -0.77527, 1e-12));
}

TEST_CASE("XTide: MLLW data minus 2.199 m is MSL data",
          "[core][datum_shift][fixture]") {
  const auto stations = read_offsets_fixture();
  const auto& narrows = stations.at("xtide:0001");
  const auto table =
      DatumTable::from_heights(narrows.reference, narrows.heights);
  REQUIRE(table.has_value());
  const TimeSeries mllw = make_series(axis_of({0}), {val(3.0)},
                                      level_meta(metre, VerticalDatum::mllw));
  const auto msl = shift(mllw, VerticalDatum::msl, *table);
  REQUIRE(msl.has_value());
  CHECK(near_abs(only(msl->samples()[0]), 3.0 - 2.199, 1e-12));
}

TEST_CASE("every F12 expectation holds for a shifted series",
          "[core][datum_shift][fixture]") {
  const auto stations = read_offsets_fixture();
  std::size_t checked = 0;
  for (const auto& [name, station] : stations) {
    if (station.bare) {
      continue;
    }
    const auto table =
        DatumTable::from_heights(station.reference, station.heights);
    REQUIRE(table.has_value());
    for (const auto& e : station.expectations) {
      INFO(name << ": " << to_string(e.from) << " -> " << to_string(e.to));
      const TimeSeries s = make_series(
          axis_of({0, 1, 2, 3, 4}),
          {val(0.0), val(1.5), val(-2.0), Sample{Dry{}}, Sample{Missing{}}},
          level_meta(metre, e.from));
      const auto r = shift(s, e.to, *table);
      REQUIRE(r.has_value());
      CHECK(near_abs(only(r->samples()[0]), e.offset, 1e-9));
      CHECK(near_abs(only(r->samples()[1]), 1.5 + e.offset, 1e-9));
      CHECK(near_abs(only(r->samples()[2]), -2.0 + e.offset, 1e-9));
      CHECK(r->samples()[3] == Sample{Dry{}});
      CHECK(r->samples()[4] == Sample{Missing{}});
      CHECK(r->meta().datum() == e.to);
      ++checked;
    }
  }
  CHECK(checked == 9);  // 5 NOAA rows, 4 XTide rows
}

TEST_CASE("the offset is converted to the unit of the series",
          "[core][datum_shift]") {
  const TimeSeries feet = make_series(axis_of({0}), {val(1.0)},
                                      level_meta(foot, VerticalDatum::msl));
  const auto r = shift(feet, VerticalDatum::mllw, battery());
  REQUIRE(r.has_value());
  CHECK(near_abs(only(r->samples()[0]), 1.0 + (0.77527 / 0.3048), 1e-12));
  CHECK(r->meta().unit() == std::optional<Unit>{LengthUnit::foot});

  const TimeSeries inches =
      make_series(axis_of({0}), {val(0.0)},
                  level_meta(Unit{LengthUnit::inch}, VerticalDatum::msl));
  const auto in = shift(inches, VerticalDatum::mllw, battery());
  REQUIRE(in.has_value());
  CHECK(near_abs(only(in->samples()[0]), 0.77527 / 0.0254, 1e-12));
}

TEST_CASE("any datum shifts to any other through MSL", "[core][datum_shift]") {
  const DatumTable table = battery();
  // NGVD29 -> NAVD88: (-0.27441) - (0.06368) = -0.33809 (F12).
  const TimeSeries ngvd = make_series(axis_of({0}), {val(1.0)},
                                      level_meta(metre, VerticalDatum::ngvd29));
  const auto navd = shift(ngvd, VerticalDatum::navd88, table);
  REQUIRE(navd.has_value());
  CHECK(near_abs(only(navd->samples()[0]), 1.0 - 0.33809, 1e-12));
}

TEST_CASE("shifting there and back restores the values",
          "[core][datum_shift]") {
  std::mt19937 rng{20261007};
  std::uniform_real_distribution<double> height{-5.0, 5.0};
  const std::array datums{VerticalDatum::mhhw, VerticalDatum::mlw,
                          VerticalDatum::navd88, VerticalDatum::stnd};
  for (int trial = 0; trial < 50; ++trial) {
    std::vector<DatumHeight> rows;
    for (const VerticalDatum d : datums) {
      rows.push_back({.datum = d, .height = metres(height(rng))});
    }
    const auto table = DatumTable::from_heights(VerticalDatum::msl, rows);
    REQUIRE(table.has_value());
    const TimeSeries s =
        make_series(axis_of({0, 1}), {val(height(rng)), val(height(rng))},
                    level_meta(metre, VerticalDatum::mhhw));
    const auto there = shift(s, VerticalDatum::stnd, *table);
    REQUIRE(there.has_value());
    const auto back = shift(*there, VerticalDatum::mhhw, *table);
    REQUIRE(back.has_value());
    CHECK(near_abs(only(back->samples()[0]), only(s.samples()[0]), 1e-12));
    CHECK(near_abs(only(back->samples()[1]), only(s.samples()[1]), 1e-12));
    CHECK(back->meta() == s.meta());
  }
}

// ---- the pinned check order
// ---------------------------------------------------

TEST_CASE(
    "shift checks the unit, then the datum, then equality, then the offset",
    "[core][datum_shift]") {
  const DatumTable empty;  // knows MSL only

  // 1. Not a length: before everything, including a missing datum.
  const auto wind = SeriesMeta::make(
      {.quantity = Quantity::wind_speed, .unit = SpeedUnit::meter_per_second});
  CHECK(shift(make_series(axis_of({0}), {val(1.0)}, wind), VerticalDatum::mllw,
              empty) == std::unexpected{ShiftError{NotALengthSeries{}}});
  const auto no_unit = level_meta(std::nullopt, VerticalDatum::msl);
  CHECK(shift(make_series(axis_of({0}), {val(1.0)}, no_unit),
              VerticalDatum::mllw,
              empty) == std::unexpected{ShiftError{NotALengthSeries{}}});
  // A generic series can carry a datum, but a temperature is no length.
  const auto celsius =
      level_meta(Unit{TemperatureUnit::celsius}, VerticalDatum::msl, "t",
                 GenericQuantity::value());
  CHECK(shift(make_series(axis_of({0}), {val(1.0)}, celsius),
              VerticalDatum::mllw,
              empty) == std::unexpected{ShiftError{NotALengthSeries{}}});

  // 2. A length with no datum.
  const auto no_datum = level_meta(metre, std::nullopt);
  CHECK(shift(make_series(axis_of({0}), {val(1.0)}, no_datum),
              VerticalDatum::mllw,
              empty) == std::unexpected{ShiftError{UnknownSourceDatum{}}});
  CHECK(shift(make_series(axis_of({0}), {val(1.0)}, no_datum),
              VerticalDatum::msl,
              empty) == std::unexpected{ShiftError{UnknownSourceDatum{}}});

  // 3. from == to returns the series, even if the table knows nothing of it.
  const TimeSeries in_navd =
      make_series(axis_of({0, 1}), {val(-0.0), Sample{Dry{}}},
                  level_meta(metre, VerticalDatum::navd88));
  const auto same = shift(in_navd, VerticalDatum::navd88, empty);
  REQUIRE(same.has_value());
  CHECK(*same == in_navd);
  CHECK(std::signbit(only(same->samples()[0])));

  // 4. The offset: `from` is reported before `to`, and a gap is never zero.
  CHECK(shift(in_navd, VerticalDatum::mllw, empty) ==
        std::unexpected{ShiftError{MissingOffset{VerticalDatum::navd88}}});
  const DatumTable knows_navd =
      table_of(VerticalDatum::msl,
               {{.datum = VerticalDatum::navd88, .height = metres(0.1)}});
  CHECK(shift(in_navd, VerticalDatum::mllw, knows_navd) ==
        std::unexpected{ShiftError{MissingOffset{VerticalDatum::mllw}}});
  const TimeSeries in_mllw = make_series(
      axis_of({0}), {val(1.0)}, level_meta(metre, VerticalDatum::mllw));
  CHECK(shift(in_mllw, VerticalDatum::navd88, knows_navd) ==
        std::unexpected{ShiftError{MissingOffset{VerticalDatum::mllw}}});
}

// ---- values, metadata, overflow
// -----------------------------------------------

TEST_CASE("shift keeps times, Missing, Dry and the other metadata",
          "[core][datum_shift]") {
  const TimeSeries s = make_series(
      axis_of({5, 10, 15}), {val(1.0), Sample{Dry{}}, Sample{Missing{}}},
      level_meta(metre, VerticalDatum::msl, "predicted level",
                 Quantity::water_level_prediction));
  const TimeSeries copy = s;
  const auto r = shift(s, VerticalDatum::mhhw, battery());
  REQUIRE(r.has_value());
  CHECK(s == copy);  // the input is a value: unchanged
  CHECK(std::ranges::equal(r->times(), s.times()));
  CHECK(r->samples()[1] == Sample{Dry{}});
  CHECK(r->samples()[2] == Sample{Missing{}});
  CHECK(r->meta() == level_meta(metre, VerticalDatum::mhhw, "predicted level",
                                Quantity::water_level_prediction));
  CHECK(near_abs(only(r->samples()[0]), 1.0 - 0.75048, 1e-12));
}

TEST_CASE("shift works on a generic series with a datum",
          "[core][datum_shift]") {
  const auto meta =
      level_meta(metre, VerticalDatum::msl, "value", GenericQuantity::value());
  const auto r = shift(make_series(axis_of({0}), {val(0.0)}, meta),
                       VerticalDatum::mllw, battery());
  REQUIRE(r.has_value());
  CHECK(near_abs(only(r->samples()[0]), 0.77527, 1e-12));
  CHECK(r->meta().quantity() == meta.quantity());
}

TEST_CASE("shift turns a value that overflows into Missing",
          "[core][datum_shift]") {
  const DatumTable table =
      table_of(VerticalDatum::msl,
               {{.datum = VerticalDatum::mhhw, .height = metres(1.7e308)}});
  const TimeSeries s = make_series(axis_of({0, 1}), {val(1.7e308), val(1.0)},
                                   level_meta(metre, VerticalDatum::mhhw));
  const auto r = shift(s, VerticalDatum::msl, table);
  REQUIRE(r.has_value());
  CHECK(samples_of(*r) ==
        std::vector<Sample>{Sample{Missing{}}, val(1.0 + 1.7e308)});
}

TEST_CASE("shift of an empty series changes only the datum",
          "[core][datum_shift]") {
  const auto r =
      shift(make_series({}, {}, level_meta(metre, VerticalDatum::msl)),
            VerticalDatum::mllw, battery());
  REQUIRE(r.has_value());
  CHECK(r->empty());
  CHECK(r->meta().datum() == VerticalDatum::mllw);
}
