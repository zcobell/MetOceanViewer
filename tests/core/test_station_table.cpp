// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mov/core/geo.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"
#include "test_helpers.hpp"

using mov::core::AxisOutOfRange;
using mov::core::Column;
using mov::core::ColumnIndex;
using mov::core::ColumnLengthMismatch;
using mov::core::DataSource;
using mov::core::Dry;
using mov::core::DuplicateStationId;
using mov::core::FileStation;
using mov::core::GenericQuantity;
using mov::core::LengthUnit;
using mov::core::Location;
using mov::core::max_abs_time_ms;
using mov::core::Quantity;
using mov::core::Sample;
using mov::core::SchemaErrc;
using mov::core::SchemaError;
using mov::core::SchemaMismatch;
using mov::core::SelectionError;
using mov::core::SeriesMeta;
using mov::core::SpeedUnit;
using mov::core::StationError;
using mov::core::StationFault;
using mov::core::StationIndex;
using mov::core::StationRow;
using mov::core::StationSelection;
using mov::core::StationTable;
using mov::core::TableError;
using mov::core::Time;
using mov::core::TimeAxis;
using mov::core::TimeNotIncreasing;
using mov::core::TimeOutOfRange;
using mov::core::TimeSeries;
using mov::core::Variable;
using mov::test::at_ms;

namespace {

Sample val(double v) { return Sample::of(v).value_or(Sample{}); }

TimeAxis axis(std::initializer_list<std::int64_t> ms) {
  TimeAxis out;
  for (const std::int64_t m : ms) {
    out.push_back(at_ms(m));
  }
  return out;
}

FileStation station(std::string id, std::string name = "name") {
  return {.id = mov::core::StationKey::make(std::move(id)).value(),
          .name = mov::core::StationText::make(std::move(name)).value(),
          .location = Location::make({.lat = 29.0, .lon = -90.0}).value(),
          .native = std::nullopt,
          .source = DataSource::adcirc};
}

SeriesMeta meta(mov::core::QuantityId q) {
  return SeriesMeta::make({.quantity = std::move(q)});
}

const SeriesMeta u_meta = SeriesMeta::make(
    {.quantity = Quantity::current_u, .unit = SpeedUnit::meter_per_second});
const SeriesMeta v_meta = SeriesMeta::make(
    {.quantity = Quantity::current_v, .unit = SpeedUnit::meter_per_second});

StationRow row(std::string id, std::size_t axis_index) {
  return {.station = station(std::move(id)), .axis = axis_index};
}

Variable variable(SeriesMeta m, std::vector<Column> per_station) {
  return {.meta = std::move(m), .per_station = std::move(per_station)};
}

/// One variable with `n` stations of `length` Missing samples each.
Variable blank(SeriesMeta m, std::size_t n, std::size_t length) {
  return variable(std::move(m), std::vector<Column>(n, Column(length)));
}

TableError schema_error(SchemaErrc code, std::size_t k) {
  return SchemaError{.code = code, .column = ColumnIndex{k}};
}

TableError station_error(std::size_t i, StationFault fault) {
  return StationError{.station = StationIndex{i}, .fault = fault};
}

constexpr StationIndex s0{0};
constexpr StationIndex s1{1};
constexpr ColumnIndex c0{0};
constexpr ColumnIndex c1{1};

/// A valid two-station, two-column table on one shared axis.
StationTable model_table() {
  auto t = StationTable::make(
      {variable(u_meta, {{val(0.1), val(0.2), Sample{}},
                         {Sample{Dry{}}, val(0.5), val(0.6)}}),
       variable(v_meta, {{val(1.0), val(2.0), val(3.0)},
                         {val(-1.0), Sample{}, val(-3.0)}})},
      {axis({0, 3600000, 7200000})}, {row("0", 0), row("1", 0)});
  REQUIRE(t.has_value());
  return t.value_or(StationTable{});
}

}  // namespace

TEST_CASE("an empty table", "[core][station_table]") {
  const StationTable t;
  CHECK(t.size() == 0);
  CHECK(t.schema().empty());
  CHECK(t.stations().empty());
  CHECK(t.total_samples() == 0);
  CHECK(not t.single_axis());
  CHECK(StationTable::make({}, {}, {}) == StationTable{});
}

TEST_CASE("a table exposes stations, times and columns",
          "[core][station_table]") {
  const StationTable t = model_table();
  REQUIRE(t.size() == 2);
  CHECK(t.schema().size() == 2);
  CHECK(t.schema()[1] == v_meta);
  CHECK(t.station(s1).id.view() == "1");
  CHECK(TimeAxis(t.times(s1).begin(), t.times(s1).end()) ==
        axis({0, 3600000, 7200000}));
  CHECK(Column(t.column(s1, c0).begin(), t.column(s1, c0).end()) ==
        Column{Sample{Dry{}}, val(0.5), val(0.6)});
  CHECK(t.total_samples() == 12);  // 2 stations x 3 times x 2 columns
  CHECK(t.single_axis());

  std::vector<StationIndex> seen;
  for (const StationIndex i : t.stations()) {
    seen.push_back(i);
  }
  CHECK(seen == std::vector<StationIndex>{s0, s1});
}

TEST_CASE("column_of finds a column by quantity token",
          "[core][station_table]") {
  const StationTable t = model_table();
  CHECK(t.column_of(Quantity::current_v) == std::optional{c1});
  CHECK(t.column_of(Quantity::current_u) == std::optional{c0});
  CHECK(not t.column_of(Quantity::water_level).has_value());
  CHECK(not t.column_of(GenericQuantity::value()).has_value());
}

TEST_CASE("series(i, k) materializes one column", "[core][station_table]") {
  const StationTable t = model_table();
  const TimeSeries s = t.series(s0, c1);
  CHECK(s.meta() == v_meta);
  CHECK(s == TimeSeries::make(axis({0, 3600000, 7200000}),
                              {val(1.0), val(2.0), val(3.0)}, v_meta)
                 .value_or(TimeSeries{}));
}

TEST_CASE("schema quantities are unique by token", "[core][station_table]") {
  CHECK(StationTable::make({blank(meta(Quantity::water_level), 0, 0),
                            blank(meta(Quantity::wind_u), 0, 0),
                            blank(meta(Quantity::water_level), 0, 0)},
                           {}, {}) ==
        std::unexpected{schema_error(SchemaErrc::duplicate_quantity, 2)});

  // Same token, different standard names: equal tokens collide, because both
  // would name the same file variable.
  const auto a =
      GenericQuantity::parse({.token = "salinity", .standard_name = ""});
  const auto b = GenericQuantity::parse(
      {.token = "salinity", .standard_name = "sea_water_salinity"});
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());
  CHECK(StationTable::make({blank(meta(a.value_or(GenericQuantity{})), 0, 0),
                            blank(meta(b.value_or(GenericQuantity{})), 0, 0)},
                           {}, {}) ==
        std::unexpected{schema_error(SchemaErrc::duplicate_quantity, 1)});

  // Labels or units do not make a column different.
  CHECK(
      StationTable::make({blank(meta(Quantity::wind_u), 0, 0),
                          blank(meta(Quantity::wind_u).with_label("o"), 0, 0)},
                         {}, {}) ==
      std::unexpected{schema_error(SchemaErrc::duplicate_quantity, 1)});
}

TEST_CASE("every variable has one column per station",
          "[core][station_table]") {
  CHECK(StationTable::make({blank(u_meta, 2, 1), blank(v_meta, 1, 1)},
                           {axis({1})}, {row("a", 0), row("b", 0)}) ==
        std::unexpected{schema_error(SchemaErrc::station_count_mismatch, 1)});
  CHECK(StationTable::make({blank(u_meta, 1, 0)}, {}, {}) ==
        std::unexpected{schema_error(SchemaErrc::station_count_mismatch, 0)});
}

TEST_CASE("each row's axis must exist", "[core][station_table]") {
  CHECK(StationTable::make({blank(u_meta, 2, 1)}, {axis({1})},
                           {row("a", 0), row("b", 1)}) ==
        std::unexpected{station_error(1, AxisOutOfRange{})});
}

TEST_CASE("axes must be strictly increasing and in range",
          "[core][station_table]") {
  CHECK(StationTable::make({blank(u_meta, 2, 2)}, {axis({1, 2}), axis({1, 1})},
                           {row("a", 0), row("b", 1)}) ==
        std::unexpected{station_error(1, TimeNotIncreasing{.index = 1})});
  CHECK(StationTable::make({blank(u_meta, 1, 2)},
                           {axis({0, max_abs_time_ms + 1})}, {row("a", 0)}) ==
        std::unexpected{station_error(0, TimeOutOfRange{.index = 1})});
  CHECK(StationTable::make({blank(u_meta, 1, 2)},
                           {axis({-max_abs_time_ms - 1, 0})}, {row("a", 0)}) ==
        std::unexpected{station_error(0, TimeOutOfRange{.index = 0})});
  // The first bad element wins, whatever is wrong with it.
  CHECK(StationTable::make({blank(u_meta, 1, 3)},
                           {axis({5, 4, max_abs_time_ms + 1})},
                           {row("a", 0)}) ==
        std::unexpected{station_error(0, TimeNotIncreasing{.index = 1})});
  CHECK(StationTable::make({blank(u_meta, 1, 3)},
                           {axis({5, max_abs_time_ms + 1, 0})},
                           {row("a", 0)}) ==
        std::unexpected{station_error(0, TimeOutOfRange{.index = 1})});
  // The bound itself is fine.
  CHECK(StationTable::make({blank(u_meta, 1, 2)},
                           {axis({-max_abs_time_ms, max_abs_time_ms})},
                           {row("a", 0)})
            .has_value());
  // The error names the first station that uses the bad axis.
  CHECK(StationTable::make(
            {variable(u_meta, {Column(1), Column(1), Column(2), Column(2)})},
            {axis({1}), axis({3, 2})},
            {row("a", 0), row("b", 0), row("c", 1), row("d", 1)}) ==
        std::unexpected{station_error(2, TimeNotIncreasing{.index = 1})});
}

TEST_CASE("an unused axis is dropped, not checked", "[core][station_table]") {
  const auto t =
      StationTable::make({variable(u_meta, {{val(1.0), val(2.0)}})},
                         {axis({5, 4}), axis({1, 2})}, {row("a", 1)});
  REQUIRE(t.has_value());
  CHECK(t == StationTable::make({variable(u_meta, {{val(1.0), val(2.0)}})},
                                {axis({1, 2})}, {row("a", 0)}));
}

TEST_CASE("each column is as long as its station's axis",
          "[core][station_table]") {
  CHECK(StationTable::make({blank(u_meta, 1, 2), variable(v_meta, {Column(3)})},
                           {axis({1, 2})}, {row("a", 0)}) ==
        std::unexpected{
            station_error(0, ColumnLengthMismatch{.column = ColumnIndex{1}})});
  // A station with no samples is legal (SN 12.8: an all-fill L2 row).
  const auto empty_row = StationTable::make(
      {variable(u_meta, {Column(2), Column{}}),
       variable(v_meta, {Column(2), Column{}})},
      {axis({1, 2}), TimeAxis{}}, {row("a", 0), row("b", 1)});
  REQUIRE(empty_row.has_value());
  CHECK(empty_row->total_samples() == 4);
  CHECK(not empty_row->single_axis());
}

TEST_CASE("station ids are unique", "[core][station_table]") {
  CHECK(StationTable::make({blank(u_meta, 3, 1)}, {axis({1})},
                           {row("x", 0), row("y", 0), row("x", 0)}) ==
        std::unexpected{station_error(2, DuplicateStationId{})});
  // Names may repeat, and may be empty (the SN writer substitutes one).
  const FileStation unnamed = station("z", "");
  CHECK(StationTable::make({blank(u_meta, 2, 1)}, {axis({1})},
                           {row("x", 0), {.station = unnamed, .axis = 0}})
            .has_value());
}

TEST_CASE("checks run in a pinned order", "[core][station_table]") {
  // Variables first, then stations in order; within a station: id, axis,
  // axis contents, column lengths.
  CHECK(StationTable::make({blank(u_meta, 1, 0), blank(u_meta, 1, 0)},
                           {axis({2, 1})}, {row("a", 9)}) ==
        std::unexpected{schema_error(SchemaErrc::duplicate_quantity, 1)});
  CHECK(StationTable::make({blank(u_meta, 2, 5)}, {axis({2, 1})},
                           {row("a", 9), row("a", 9)}) ==
        std::unexpected{station_error(0, AxisOutOfRange{})});
  CHECK(StationTable::make({blank(u_meta, 2, 5)}, {axis({1, 2})},
                           {row("a", 0), row("a", 9)}) ==
        std::unexpected{station_error(0, ColumnLengthMismatch{.column = c0})});
  CHECK(StationTable::make({blank(u_meta, 2, 5)}, {axis({2, 1})},
                           {row("a", 0), row("a", 0)}) ==
        std::unexpected{station_error(0, TimeNotIncreasing{.index = 1})});
  CHECK(StationTable::make({blank(u_meta, 2, 2)}, {axis({1, 2})},
                           {row("a", 0), row("a", 9)}) ==
        std::unexpected{station_error(1, DuplicateStationId{})});
}

TEST_CASE("single_axis compares time vectors, not pool slots",
          "[core][station_table]") {
  const SeriesMeta level = meta(Quantity::water_level);
  const auto shared = StationTable::make(
      {blank(level, 2, 2)}, {axis({0, 60000})}, {row("a", 0), row("b", 0)});
  const auto equal = StationTable::make({blank(level, 2, 2)},
                                        {axis({0, 60000}), axis({0, 60000})},
                                        {row("a", 0), row("b", 1)});
  const auto one_ms_apart = StationTable::make(
      {blank(level, 2, 2)}, {axis({0, 60000}), axis({0, 60001})},
      {row("a", 0), row("b", 1)});
  const auto one_station =
      StationTable::make({blank(level, 1, 1)}, {axis({0})}, {row("a", 0)});
  const auto no_times =
      StationTable::make({blank(level, 1, 0)}, {TimeAxis{}}, {row("a", 0)});
  REQUIRE(shared.has_value());
  REQUIRE(equal.has_value());
  REQUIRE(one_ms_apart.has_value());
  REQUIRE(one_station.has_value());
  REQUIRE(no_times.has_value());
  CHECK(shared->single_axis());
  CHECK(equal->single_axis());
  CHECK(not one_ms_apart->single_axis());
  CHECK(one_station->single_axis());
  CHECK(not no_times->single_axis());
  // Equality is by value: sharing an axis or storing it twice is the same.
  CHECK(shared == equal);
  CHECK(shared != one_ms_apart);
}

TEST_CASE("table equality compares schema, stations, times and samples",
          "[core][station_table]") {
  const StationTable t = model_table();
  CHECK(t == model_table());
  const auto with = [](SeriesMeta u, Column last, std::string id) {
    return StationTable::make(
        {variable(std::move(u), {{val(0.1), val(0.2), Sample{}},
                                 {Sample{Dry{}}, val(0.5), val(0.6)}}),
         variable(v_meta, {{val(1.0), val(2.0), val(3.0)}, std::move(last)})},
        {axis({0, 3600000, 7200000})}, {row("0", 0), row(std::move(id), 0)});
  };
  const Column same{val(-1.0), Sample{}, val(-3.0)};
  CHECK(t == with(u_meta, same, "1"));
  CHECK(t != with(u_meta.with_label("u"), same, "1"));
  CHECK(t != with(u_meta, {val(-1.0), Sample{}, val(3.0)}, "1"));
  CHECK(t != with(u_meta, same, "2"));
  CHECK(t !=
        StationTable::make({variable(u_meta, {{val(0.1), val(0.2), Sample{}}}),
                            variable(v_meta, {{val(1.0), val(2.0), val(3.0)}})},
                           {axis({0, 3600000, 7200000})}, {row("0", 0)}));
  CHECK(t != StationTable::make(
                 {variable(u_meta, {{val(0.1), val(0.2), Sample{}},
                                    {Sample{Dry{}}, val(0.5), val(0.6)}}),
                  variable(v_meta, {{val(1.0), val(2.0), val(3.0)}, same})},
                 {axis({0, 3600000, 7200000}), axis({0, 3600000, 7200001})},
                 {row("0", 0), row("1", 1)}));
}

TEST_CASE("from_series builds a one-column table by moving the series",
          "[core][station_table]") {
  const SeriesMeta level = SeriesMeta::make({.quantity = Quantity::water_level,
                                             .label = "obs",
                                             .unit = LengthUnit::foot});
  const TimeSeries a =
      TimeSeries::make(axis({0, 10}), {val(1.0), val(2.0)}, level)
          .value_or(TimeSeries{});
  const TimeSeries b =
      TimeSeries::make(axis({5}), {Sample{}}, level).value_or(TimeSeries{});
  const auto t =
      StationTable::from_series(level, {{.station = station("A"), .data = a},
                                        {.station = station("B"), .data = b}});
  REQUIRE(t.has_value());
  REQUIRE(t->size() == 2);
  CHECK(t->schema().size() == 1);
  CHECK(t->schema()[0] == level);
  CHECK(t->series(s0, c0) == a);
  CHECK(t->series(s1, c0) == b);
  CHECK(t->station(s1) == station("B"));
  CHECK(not t->single_axis());

  // No series: the declared schema and no stations.
  const auto empty = StationTable::from_series(level, {});
  REQUIRE(empty.has_value());
  CHECK(empty->size() == 0);
  CHECK(empty->schema().size() == 1);
  CHECK(empty->schema()[0] == level);
}

TEST_CASE("from_series points at the series that differ from the schema",
          "[core][station_table]") {
  const SeriesMeta level = meta(Quantity::water_level);
  const TimeSeries a =
      TimeSeries::make(axis({0}), {val(1.0)}, level).value_or(TimeSeries{});
  CHECK(StationTable::from_series(
            level, {{.station = station("A"), .data = a},
                    {.station = station("B"), .data = a},
                    {.station = station("C"), .data = a.with_label("x")}}) ==
        std::unexpected{station_error(2, SchemaMismatch{})});
  CHECK(StationTable::from_series(level.with_label("declared"),
                                  {{.station = station("A"), .data = a}}) ==
        std::unexpected{station_error(0, SchemaMismatch{})});
  // The other checks are make's.
  CHECK(StationTable::from_series(level,
                                  {{.station = station("A"), .data = a},
                                   {.station = station("A"), .data = a}}) ==
        std::unexpected{station_error(1, DuplicateStationId{})});
  // TimeSeries -> StationTable is partial: file times are bounded (SN 7).
  const TimeSeries far =
      TimeSeries::make({Time{std::chrono::milliseconds{max_abs_time_ms + 1}}},
                       {val(1.0)}, level)
          .value_or(TimeSeries{});
  CHECK(StationTable::from_series(level,
                                  {{.station = station("A"), .data = far}}) ==
        std::unexpected{station_error(0, TimeOutOfRange{.index = 0})});
}

TEST_CASE("StationSelection holds distinct indices in caller order",
          "[core][station_table]") {
  const auto s = StationSelection::make({4, 0, 2}, 5);
  REQUIRE(s.has_value());
  CHECK(std::vector<std::size_t>(s->indices().begin(), s->indices().end()) ==
        std::vector<std::size_t>{4, 0, 2});
  CHECK(s->station_count() == 5);
  CHECK(StationSelection::make({}, 0).has_value());

  CHECK(StationSelection::make({1, 5}, 5) ==
        std::unexpected{SelectionError::out_of_range});
  CHECK(StationSelection::make({0}, 0) ==
        std::unexpected{SelectionError::out_of_range});
  CHECK(StationSelection::make({3, 1, 3}, 5) ==
        std::unexpected{SelectionError::duplicate_index});
  // Out of range is reported before a duplicate.
  CHECK(StationSelection::make({1, 1, 7}, 5) ==
        std::unexpected{SelectionError::out_of_range});
}

TEST_CASE("a selection knows the station count it was made for",
          "[core][station_table]") {
  const auto s = StationSelection::make({1}, 5);
  REQUIRE(s.has_value());
  CHECK(s->applies_to(5).has_value());
  CHECK(s->applies_to(6) ==
        std::unexpected{SelectionError::selection_mismatch});
  // The count is part of the value.
  CHECK(s != StationSelection::make({1}, 6));
  CHECK(s == StationSelection::make({1}, 5));
}

TEST_CASE("StationSelection::all selects every station in order",
          "[core][station_table]") {
  const StationSelection all = StationSelection::all(3);
  CHECK(std::vector<std::size_t>(all.indices().begin(), all.indices().end()) ==
        std::vector<std::size_t>{0, 1, 2});
  CHECK(all.station_count() == 3);
  CHECK(all == StationSelection::make({0, 1, 2}, 3));
  const StationSelection none = StationSelection::all(0);
  CHECK(none.indices().empty());
}
