// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "station_nc_canonical.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/units.hpp"

namespace mov::test::station_nc {

namespace {

template <class T>
T must(std::optional<T> value, const char* what) {
  if (not value) {
    throw std::runtime_error{what};
  }
  return *std::move(value);
}

template <class T, class E>
T must(std::expected<T, E> value, const char* what) {
  if (not value) {
    throw std::runtime_error{what};
  }
  return *std::move(value);
}

core::Sample v(double x) { return must(core::Sample::of(x), "sample"); }
const core::Sample dry{core::Dry{}};
const core::Sample missing{};

core::Time ms(std::int64_t t) {
  return core::Time{std::chrono::milliseconds{t}};
}

core::Unit unit(std::string_view text) {
  return must(core::parse_unit(text), "unit");
}

core::FileStation station(std::string id, std::string name, double lat,
                          double lon) {
  return {.id = must(core::StationKey::make(std::move(id)), "id"),
          .name = must(core::StationText::make(std::move(name)), "name"),
          .location =
              must(core::Location::make({.lat = lat, .lon = lon}), "location"),
          .native = std::nullopt,
          .source = core::DataSource::noaa_coops};
}

core::SeriesMeta meta(core::QuantityId q, std::string label,
                      std::optional<std::string_view> unit_text,
                      std::optional<core::VerticalDatum> datum = {}) {
  core::SeriesMeta m = core::SeriesMeta::make(
      {.quantity = std::move(q),
       .label = std::move(label),
       .unit = unit_text ? std::optional{unit(*unit_text)} : std::nullopt});
  if (datum) {
    m = must(m.assume_datum(*datum), "datum");
  }
  return m;
}

core::SeriesMeta water_level() {
  return meta(core::Quantity::water_level, "water level", "m",
              core::VerticalDatum::mllw);
}

core::SeriesMeta water_temperature() {
  return meta(core::Quantity::water_temperature, "water temperature", "degC");
}

std::vector<core::StationRow> two_stations(std::size_t axis_a,
                                           std::size_t axis_b) {
  return {{.station = station("8761724", "Grand Isle, LA", 29.2633, -89.9567),
           .axis = axis_a},
          {.station = station("8760922", "Pilots Station East, SW Pass",
                              28.9322, -89.4067),
           .axis = axis_b}};
}

io::StationNcWriteOptions example_options() {
  return {.title = "Example",
          .institution = "X",
          .source = "NOAA CO-OPS API",
          .references = std::nullopt,
          .comment = std::nullopt};
}

core::StationTable table(std::vector<core::Variable> variables,
                         std::vector<core::TimeAxis> axes,
                         std::vector<core::StationRow> rows) {
  return must(core::StationTable::make(std::move(variables), std::move(axes),
                                       std::move(rows)),
              "table");
}

}  // namespace

std::chrono::sys_seconds canonical_now() {
  return std::chrono::sys_seconds{std::chrono::sys_days{
             std::chrono::year{2026} / std::chrono::October /
             std::chrono::day{6}}} +
         std::chrono::hours{12};
}

Canonical orthogonal() {
  const std::int64_t t0 = 1700000000000;
  std::vector<core::TimeAxis> axes{
      {ms(t0), ms(t0 + 360000), ms(t0 + 720000), ms(t0 + 1080000)}};
  std::vector<core::Variable> variables{
      {.meta = water_level(),
       .per_station = {{v(0.5), v(0.6), v(0.7), v(0.8)},
                       {v(1), dry, v(1.2), v(1.3)}}},
      {.meta = water_temperature(),
       .per_station = {{v(20), v(21), v(22), v(23.5)},
                       {missing, missing, missing, missing}}}};
  return {
      .name = "station_timeseries_orthogonal",
      .table = table(std::move(variables), std::move(axes), two_stations(0, 0)),
      .options = example_options()};
}

Canonical incomplete() {
  const std::int64_t t0 = 1700000000000;
  std::vector<core::TimeAxis> axes{{ms(t0), ms(t0 + 360000), ms(t0 + 720000)},
                                   {ms(t0), ms(t0 + 900000), ms(t0 + 1800000),
                                    ms(t0 + 2700000), ms(t0 + 3600000)}};
  std::vector<core::Variable> variables{
      {.meta = water_level(),
       .per_station = {{v(0.5), v(0.6), v(0.7)},
                       {v(1), dry, v(1.2), v(1.3), v(1.4)}}},
      {.meta = water_temperature(),
       .per_station = {{v(20), v(21), v(22)},
                       {missing, missing, missing, missing, missing}}}};
  return {
      .name = "station_timeseries_incomplete",
      .table = table(std::move(variables), std::move(axes), two_stations(0, 1)),
      .options = example_options()};
}

Canonical registry() {
  const std::int64_t t0 = 1700000000000;
  core::TimeAxis axis{ms(t0), ms(t0 + 3600000)};
  std::vector<core::Variable> variables;
  for (std::size_t q = 0; q < core::detail::quantity_registry.size(); ++q) {
    const auto quantity = static_cast<core::Quantity>(q);
    const core::QuantityInfo row = core::info(quantity);
    std::optional<std::string_view> unit_text = row.canonical_unit;
    if (quantity == core::Quantity::difference) {
      unit_text = "m";  // the unit of its operands
    }
    std::optional<core::VerticalDatum> datum;
    if (core::datum_applicable(core::QuantityId{quantity})) {
      datum = core::VerticalDatum::navd88;
    }
    variables.push_back(
        {.meta = meta(quantity, std::string{row.long_name}, unit_text, datum),
         .per_station = {{v(1.0), v(2.0)}}});
  }
  variables.push_back(
      {.meta = meta(core::GenericQuantity::value(), "gauge reading", "ft",
                    core::VerticalDatum::stnd),
       .per_station = {{v(3.25), missing}}});
  variables.push_back(
      {.meta = meta(must(core::GenericQuantity::parse(
                             {.token = "sea_water_x_velocity",
                              .standard_name = "sea_water_x_velocity"}),
                         "generic"),
                    "grid-relative current x", "m s-1"),
       .per_station = {{v(-0.5), v(0.25)}}});
  variables.push_back(
      {.meta =
           meta(must(core::GenericQuantity::parse(
                         {.token = "probe_temperature", .standard_name = ""}),
                     "generic"),
                "probe temperature", "degF"),
       .per_station = {{v(50.0), v(51.5)}}});
  std::vector<core::StationRow> rows{
      {.station = station("8761724", "Grand Isle, LA", 29.2633, -89.9567),
       .axis = 0}};
  return {
      .name = "station_timeseries_registry",
      .table = table(std::move(variables), {std::move(axis)}, std::move(rows)),
      .options = {.title = "Every registry quantity",
                  .institution = std::nullopt,
                  .source = "MetOceanViewer test fixture",
                  .references = "https://cfconventions.org",
                  .comment = "values illustrative"}};
}

std::vector<Canonical> all() {
  return {orthogonal(), incomplete(), registry()};
}

std::filesystem::path write(const Canonical& c,
                            const std::filesystem::path& dir) {
  const std::filesystem::path path = dir / (std::string{c.name} + ".nc");
  const auto written =
      io::write_station_netcdf(path, c.table, c.options, canonical_now());
  if (not written) {
    throw std::runtime_error{"write_station_netcdf failed for " +
                             path.string()};
  }
  return path;
}

}  // namespace mov::test::station_nc
