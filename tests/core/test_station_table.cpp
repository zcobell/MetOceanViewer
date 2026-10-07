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

using mov::core::Column;
using mov::core::DataSource;
using mov::core::Dry;
using mov::core::FileStation;
using mov::core::GenericQuantity;
using mov::core::LengthUnit;
using mov::core::Location;
using mov::core::max_abs_time_ms;
using mov::core::Quantity;
using mov::core::Sample;
using mov::core::SelectionError;
using mov::core::SeriesMeta;
using mov::core::SpeedUnit;
using mov::core::StationRow;
using mov::core::StationSelection;
using mov::core::StationTable;
using mov::core::TableErrc;
using mov::core::TableError;
using mov::core::Time;
using mov::core::TimeSeries;
using mov::test::at_ms;

namespace {

using Axis = std::vector<Time>;

Sample val(double v) { return Sample::of(v).value_or(Sample{}); }

Axis axis(std::initializer_list<std::int64_t> ms) {
  Axis out;
  for (const std::int64_t m : ms) {
    out.push_back(at_ms(m));
  }
  return out;
}

FileStation station(std::string id, std::string name = "name") {
  return {.id = std::move(id),
          .name = std::move(name),
          .location = Location::make({.lat = 29.0, .lon = -90.0}).value(),
          .native = std::nullopt,
          .source = DataSource::adcirc};
}

SeriesMeta meta(mov::core::QuantityId q) {
  return SeriesMeta::make({.quantity = std::move(q)}).value_or(SeriesMeta{});
}

std::vector<SeriesMeta> uv_schema() {
  return {SeriesMeta::make({.quantity = Quantity::current_u,
                            .unit = SpeedUnit::meter_per_second})
              .value_or(SeriesMeta{}),
          SeriesMeta::make({.quantity = Quantity::current_v,
                            .unit = SpeedUnit::meter_per_second})
              .value_or(SeriesMeta{})};
}

StationRow row(std::string id, std::size_t axis_index,
               std::vector<Column> columns) {
  return {.station = station(std::move(id)),
          .axis = axis_index,
          .columns = std::move(columns)};
}

TableError error(TableErrc code, std::optional<std::size_t> station_index,
                 std::optional<std::size_t> index) {
  return {.code = code, .station = station_index, .index = index};
}

std::expected<StationTable, TableError> one_station(FileStation s) {
  return StationTable::make(
      {meta(GenericQuantity::value())}, {axis({1})},
      {{.station = std::move(s), .axis = 0, .columns = {Column{val(1.0)}}}});
}

/// A valid two-station, two-column table on one shared axis.
StationTable model_table() {
  auto t = StationTable::make(
      uv_schema(), {axis({0, 3600000, 7200000})},
      {row("0", 0,
           {{val(0.1), val(0.2), Sample{}}, {val(1.0), val(2.0), val(3.0)}}),
       row("1", 0,
           {{Sample{Dry{}}, val(0.5), val(0.6)},
            {val(-1.0), Sample{}, val(-3.0)}})});
  REQUIRE(t.has_value());
  return t.value_or(StationTable{});
}

}  // namespace

TEST_CASE("an empty table", "[core][station_table]") {
  const StationTable t;
  CHECK(t.size() == 0);
  CHECK(t.schema().empty());
  CHECK(t.total_samples() == 0);
  CHECK(not t.single_axis());
  CHECK(StationTable::make({}, {}, {}) == StationTable{});
}

TEST_CASE("a table exposes stations, times and columns",
          "[core][station_table]") {
  const StationTable t = model_table();
  REQUIRE(t.size() == 2);
  CHECK(t.schema().size() == 2);
  CHECK(t.schema()[1] == uv_schema()[1]);
  CHECK(t.station(1).id == "1");
  CHECK(Axis(t.times(1).begin(), t.times(1).end()) ==
        axis({0, 3600000, 7200000}));
  CHECK(Column(t.column(1, 0).begin(), t.column(1, 0).end()) ==
        Column{Sample{Dry{}}, val(0.5), val(0.6)});
  CHECK(t.total_samples() == 12);  // 2 stations x 3 times x 2 columns
  CHECK(t.single_axis());
}

TEST_CASE("series(i, k) materializes one column", "[core][station_table]") {
  const StationTable t = model_table();
  const TimeSeries s = t.series(0, 1);
  CHECK(s.meta() == uv_schema()[1]);
  CHECK(s == TimeSeries::make(axis({0, 3600000, 7200000}),
                              {val(1.0), val(2.0), val(3.0)}, uv_schema()[1])
                 .value_or(TimeSeries{}));
}

TEST_CASE("schema quantities are unique by token", "[core][station_table]") {
  CHECK(StationTable::make({meta(Quantity::water_level), meta(Quantity::wind_u),
                            meta(Quantity::water_level)},
                           {}, {}) ==
        std::unexpected{error(TableErrc::duplicate_quantity, std::nullopt, 2)});

  // Same token, different standard names: equal tokens collide (WP1 note).
  const auto a =
      GenericQuantity::parse({.token = "salinity", .standard_name = ""});
  const auto b = GenericQuantity::parse(
      {.token = "salinity", .standard_name = "sea_water_salinity"});
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());
  CHECK(StationTable::make({meta(a.value_or(GenericQuantity{})),
                            meta(b.value_or(GenericQuantity{}))},
                           {}, {}) ==
        std::unexpected{error(TableErrc::duplicate_quantity, std::nullopt, 1)});

  // Labels or units do not make a column different.
  const SeriesMeta labelled = meta(Quantity::wind_u).with_label("other");
  CHECK(StationTable::make({meta(Quantity::wind_u), labelled}, {}, {})
            .error_or(TableError{})
            .code == TableErrc::duplicate_quantity);
}

TEST_CASE("each row's axis must exist", "[core][station_table]") {
  CHECK(StationTable::make(
            {meta(Quantity::wind_u)}, {axis({1})},
            {row("a", 0, {{val(1.0)}}), row("b", 1, {{val(1.0)}})}) ==
        std::unexpected{error(TableErrc::axis_out_of_range, 1, std::nullopt)});
}

TEST_CASE("axes must be strictly increasing and in range",
          "[core][station_table]") {
  const std::vector<SeriesMeta> schema{meta(Quantity::wind_u)};
  CHECK(StationTable::make(
            schema, {axis({1, 2}), axis({1, 1})},
            {row("a", 0, {Column(2)}), row("b", 1, {Column(2)})}) ==
        std::unexpected{error(TableErrc::time_not_increasing, 1, 1)});
  CHECK(StationTable::make(schema, {axis({0, max_abs_time_ms + 1})},
                           {row("a", 0, {Column(2)})}) ==
        std::unexpected{error(TableErrc::time_out_of_range, 0, 1)});
  CHECK(StationTable::make(schema, {axis({-max_abs_time_ms - 1, 0})},
                           {row("a", 0, {Column(2)})}) ==
        std::unexpected{error(TableErrc::time_out_of_range, 0, 0)});
  // The bound itself is fine.
  CHECK(StationTable::make(schema, {axis({-max_abs_time_ms, max_abs_time_ms})},
                           {row("a", 0, {Column(2)})})
            .has_value());
  // The error names the first station that uses the bad axis.
  CHECK(StationTable::make(
            schema, {axis({1}), axis({3, 2})},
            {row("a", 0, {Column(1)}), row("b", 0, {Column(1)}),
             row("c", 1, {Column(2)}), row("d", 1, {Column(2)})}) ==
        std::unexpected{error(TableErrc::time_not_increasing, 2, 1)});
}

TEST_CASE("an unused axis is dropped, not checked", "[core][station_table]") {
  const auto t =
      StationTable::make({meta(Quantity::wind_u)}, {axis({5, 4}), axis({1, 2})},
                         {row("a", 1, {{val(1.0), val(2.0)}})});
  REQUIRE(t.has_value());
  CHECK(t == StationTable::make({meta(Quantity::wind_u)}, {axis({1, 2})},
                                {row("a", 0, {{val(1.0), val(2.0)}})}));
}

TEST_CASE("every row has one column per schema entry, each as long as its axis",
          "[core][station_table]") {
  CHECK(StationTable::make(
            uv_schema(), {axis({1, 2})},
            {row("a", 0, {Column(2), Column(2)}), row("b", 0, {Column(2)})}) ==
        std::unexpected{
            error(TableErrc::column_count_mismatch, 1, std::nullopt)});
  CHECK(StationTable::make(uv_schema(), {axis({1, 2})},
                           {row("a", 0, {Column(2), Column(3)})}) ==
        std::unexpected{error(TableErrc::column_length_mismatch, 0, 1)});
  // A station with no samples is legal (SN 12.8: an all-fill L2 row).
  const auto empty_row = StationTable::make(
      uv_schema(), {axis({1, 2}), Axis{}},
      {row("a", 0, {Column(2), Column(2)}), row("b", 1, {Column{}, Column{}})});
  REQUIRE(empty_row.has_value());
  CHECK(empty_row->total_samples() == 4);
  CHECK(not empty_row->single_axis());
}

TEST_CASE("station ids are non-empty, unique, UTF-8 without NUL",
          "[core][station_table]") {
  CHECK(one_station(station("")) ==
        std::unexpected{error(TableErrc::empty_station_id, 0, std::nullopt)});
  CHECK(one_station(station(std::string{"a\0b", 3})) ==
        std::unexpected{error(TableErrc::embedded_nul, 0, std::nullopt)});
  CHECK(one_station(station("a", std::string{"name\0junk", 9})) ==
        std::unexpected{error(TableErrc::embedded_nul, 0, std::nullopt)});
  CHECK(one_station(station("\xC3\x28")) ==
        std::unexpected{error(TableErrc::invalid_utf8, 0, std::nullopt)});
  CHECK(one_station(station("a", "\xFF")) ==
        std::unexpected{error(TableErrc::invalid_utf8, 0, std::nullopt)});
  // An empty name is allowed (the SN writer substitutes "Station <id>").
  CHECK(one_station(station("a", "")).has_value());
  CHECK(one_station(station("\xC3\x89le", "\xC3\x89le")).has_value());

  CHECK(
      StationTable::make({meta(Quantity::wind_u)}, {axis({1})},
                         {row("x", 0, {Column(1)}), row("y", 0, {Column(1)}),
                          row("x", 0, {Column(1)})}) ==
      std::unexpected{error(TableErrc::duplicate_station_id, 2, std::nullopt)});
}

TEST_CASE("checks run in a pinned order", "[core][station_table]") {
  // Schema first, then rows in order; within a row: id, axis, columns.
  CHECK(StationTable::make({meta(Quantity::wind_u), meta(Quantity::wind_u)}, {},
                           {row("", 9, {})})
            .error_or(TableError{})
            .code == TableErrc::duplicate_quantity);
  CHECK(StationTable::make({meta(Quantity::wind_u)}, {axis({2, 1})},
                           {row("", 9, {})})
            .error_or(TableError{})
            .code == TableErrc::empty_station_id);
  CHECK(StationTable::make({meta(Quantity::wind_u)}, {axis({2, 1})},
                           {row("a", 9, {})})
            .error_or(TableError{})
            .code == TableErrc::axis_out_of_range);
  CHECK(StationTable::make({meta(Quantity::wind_u)}, {axis({2, 1})},
                           {row("a", 0, {})})
            .error_or(TableError{})
            .code == TableErrc::time_not_increasing);
  CHECK(StationTable::make({meta(Quantity::wind_u)}, {axis({1, 2})},
                           {row("a", 0, {}), row("", 0, {})})
            .error_or(TableError{})
            .code == TableErrc::column_count_mismatch);
}

TEST_CASE("single_axis compares time vectors, not pool slots",
          "[core][station_table]") {
  const std::vector<SeriesMeta> schema{meta(Quantity::water_level)};
  const auto shared =
      StationTable::make(schema, {axis({0, 60000})},
                         {row("a", 0, {Column(2)}), row("b", 0, {Column(2)})});
  const auto equal =
      StationTable::make(schema, {axis({0, 60000}), axis({0, 60000})},
                         {row("a", 0, {Column(2)}), row("b", 1, {Column(2)})});
  const auto one_ms_apart =
      StationTable::make(schema, {axis({0, 60000}), axis({0, 60001})},
                         {row("a", 0, {Column(2)}), row("b", 1, {Column(2)})});
  const auto one_station =
      StationTable::make(schema, {axis({0})}, {row("a", 0, {Column(1)})});
  const auto no_times =
      StationTable::make(schema, {Axis{}}, {row("a", 0, {Column{}})});
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
  const auto other_schema = StationTable::make(
      {uv_schema()[0].with_label("u"), uv_schema()[1]},
      {axis({0, 3600000, 7200000})},
      {row("0", 0,
           {{val(0.1), val(0.2), Sample{}}, {val(1.0), val(2.0), val(3.0)}}),
       row("1", 0,
           {{Sample{Dry{}}, val(0.5), val(0.6)},
            {val(-1.0), Sample{}, val(-3.0)}})});
  const auto other_sample = StationTable::make(
      uv_schema(), {axis({0, 3600000, 7200000})},
      {row("0", 0,
           {{val(0.1), val(0.2), Sample{}}, {val(1.0), val(2.0), val(3.0)}}),
       row("1", 0,
           {{Sample{Dry{}}, val(0.5), val(0.6)},
            {val(-1.0), Sample{}, val(3.0)}})});
  const auto other_station = StationTable::make(
      uv_schema(), {axis({0, 3600000, 7200000})},
      {row("0", 0,
           {{val(0.1), val(0.2), Sample{}}, {val(1.0), val(2.0), val(3.0)}}),
       row("2", 0,
           {{Sample{Dry{}}, val(0.5), val(0.6)},
            {val(-1.0), Sample{}, val(-3.0)}})});
  const auto fewer = StationTable::make(
      uv_schema(), {axis({0, 3600000, 7200000})},
      {row("0", 0,
           {{val(0.1), val(0.2), Sample{}}, {val(1.0), val(2.0), val(3.0)}})});
  CHECK(t != other_schema);
  CHECK(t != other_sample);
  CHECK(t != other_station);
  CHECK(t != fewer);
}

TEST_CASE("from_series builds a one-column table", "[core][station_table]") {
  const SeriesMeta level = SeriesMeta::make({.quantity = Quantity::water_level,
                                             .label = "obs",
                                             .unit = LengthUnit::foot})
                               .value_or(SeriesMeta{});
  const TimeSeries a =
      TimeSeries::make(axis({0, 10}), {val(1.0), val(2.0)}, level)
          .value_or(TimeSeries{});
  const TimeSeries b =
      TimeSeries::make(axis({5}), {Sample{}}, level).value_or(TimeSeries{});
  const auto t =
      StationTable::from_series({{.station = station("A"), .data = a},
                                 {.station = station("B"), .data = b}});
  REQUIRE(t.has_value());
  REQUIRE(t->size() == 2);
  CHECK(t->schema().size() == 1);
  CHECK(t->schema()[0] == level);
  CHECK(t->series(0, 0) == a);
  CHECK(t->series(1, 0) == b);
  CHECK(t->station(1) == station("B"));
  CHECK(not t->single_axis());

  CHECK(StationTable::from_series({}) == StationTable{});
}

TEST_CASE("from_series rejects differing metadata and bad stations",
          "[core][station_table]") {
  const TimeSeries a =
      TimeSeries::make(axis({0}), {val(1.0)}, meta(Quantity::water_level))
          .value_or(TimeSeries{});
  CHECK(StationTable::from_series(
            {{.station = station("A"), .data = a},
             {.station = station("B"), .data = a},
             {.station = station("C"), .data = a.with_label("x")}}) ==
        std::unexpected{error(TableErrc::schema_mismatch, 2, std::nullopt)});
  // The other checks are make's.
  CHECK(
      StationTable::from_series({{.station = station("A"), .data = a},
                                 {.station = station("A"), .data = a}}) ==
      std::unexpected{error(TableErrc::duplicate_station_id, 1, std::nullopt)});
  const TimeSeries far =
      TimeSeries::make({Time{std::chrono::milliseconds{max_abs_time_ms + 1}}},
                       {val(1.0)}, meta(Quantity::water_level))
          .value_or(TimeSeries{});
  CHECK(StationTable::from_series({{.station = station("A"), .data = far}}) ==
        std::unexpected{error(TableErrc::time_out_of_range, 0, 0)});
}

TEST_CASE("StationSelection holds distinct indices in caller order",
          "[core][station_table]") {
  const auto s = StationSelection::make({4, 0, 2}, 5);
  REQUIRE(s.has_value());
  CHECK(std::vector<std::size_t>(s->indices().begin(), s->indices().end()) ==
        std::vector<std::size_t>{4, 0, 2});
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

TEST_CASE("StationSelection::all selects every station in order",
          "[core][station_table]") {
  const StationSelection all = StationSelection::all(3);
  CHECK(std::vector<std::size_t>(all.indices().begin(), all.indices().end()) ==
        std::vector<std::size_t>{0, 1, 2});
  CHECK(all == StationSelection::make({0, 1, 2}, 3));
  const StationSelection none = StationSelection::all(0);
  CHECK(none.indices().empty());
}
