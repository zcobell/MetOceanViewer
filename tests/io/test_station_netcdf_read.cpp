// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The v5 station netCDF reader: round trips through the writer (canonical
// files, edge cases and random tables), selections, the catalog, and the
// normalizations the writer reports.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/station_netcdf.hpp"
#include "mov/io/warning.hpp"
#include "mov/test/scratch_dir.hpp"
#include "nc_edit.hpp"
#include "station_nc_support.hpp"

namespace {

using namespace mov::test::snc;  // NOLINT(google-build-using-namespace)
namespace core = mov::core;
namespace io = mov::io;
namespace station_nc = mov::test::station_nc;
using core::Quantity;
using core::VerticalDatum;
using io::FormatErrc;
using io::StationNcLayout;
using mov::test::ScratchDir;

constexpr std::array all_datums{VerticalDatum::mhhw,   VerticalDatum::mhw,
                                VerticalDatum::mtl,    VerticalDatum::msl,
                                VerticalDatum::mlw,    VerticalDatum::mllw,
                                VerticalDatum::navd88, VerticalDatum::ngvd29,
                                VerticalDatum::igld85, VerticalDatum::stnd};

constexpr std::int64_t max_ms = core::max_abs_time_ms;

/// The file can be opened again: the reader closed its handle (a leaked
/// one would assert in a debug build).
void reopens(const std::filesystem::path& path) {
  auto file = io::nc::File::open(path, {});
  REQUIRE(file.has_value());
  CHECK(std::move(*file).close().has_value());
}

io::Read<io::StationFile> round_trip(const ScratchDir& dir,
                                     const StationTable& table) {
  const auto path = dir / "rt.nc";
  const auto written = must_write(path, table);
  CHECK(written.warnings.empty());
  auto read = must_read(read_all(path));
  CHECK(layout_of(read.value) == written.layout);
  CHECK(version_of(read.value) == io::station_nc_version);
  reopens(path);
  return read;
}

TEST_CASE("the canonical files read back as the tables that were written",
          "[io][station_nc][read][roundtrip]") {
  const ScratchDir dir;
  for (const station_nc::Canonical& c : station_nc::all()) {
    CAPTURE(c.name);
    const auto path = station_nc::write(c, dir.path());
    const auto read = must_read(read_all(path));
    CHECK(read.warnings.empty());
    CHECK(read.value.table == c.table);
  }
}

TEST_CASE("round trip: Missing, Dry and values at start, middle and end",
          "[io][station_nc][read][roundtrip]") {
  const ScratchDir dir;
  const core::TimeAxis axis{t(0), t(1), t(2), t(3), t(4)};
  SECTION("orthogonal") {
    const auto table = water_levels(
        {axis, axis, axis}, {{missing, v(1), dry, missing, dry},
                             {dry, dry, dry, dry, dry},
                             {missing, missing, missing, missing, missing}});
    const auto read = round_trip(dir, table);
    CHECK(layout_of(read.value) == StationNcLayout::orthogonal);
    CHECK(read.value.table == table);
  }
  SECTION("incomplete, with an empty station") {
    const auto table = water_levels(
        {axis, {}, {t(7)}}, {{dry, v(1), missing, v(2), dry}, {}, {missing}});
    const auto read = round_trip(dir, table);
    CHECK(layout_of(read.value) == StationNcLayout::incomplete);
    CHECK(read.value.table == table);
  }
}

TEST_CASE("round trip: times at the edges of the format",
          "[io][station_nc][read][roundtrip]") {
  const ScratchDir dir;
  const core::TimeAxis edges{ms(-max_ms),       ms(-1),    ms(0), ms(1),
                             ms(1700000000001), ms(max_ms)};
  const core::TimeAxis pre_1970{ms(-2208988800000), ms(-2208988799999)};
  const auto table = water_levels(
      {edges, pre_1970}, {{v(1), v(2), v(3), v(4), v(5), v(6)}, {v(7), v(8)}});
  CHECK(round_trip(dir, table).value.table == table);
}

TEST_CASE("round trip: extreme values keep every bit",
          "[io][station_nc][read][roundtrip]") {
  const ScratchDir dir;
  const core::TimeAxis axis{t(0), t(1), t(2), t(3), t(4), t(5)};
  const double denormal = std::numeric_limits<double>::denorm_min();
  const auto table =
      water_levels({axis}, {{v(DBL_MAX), v(-DBL_MAX), v(-0.0), v(denormal),
                             v(-denormal), v(9.969209968386868e36)}});
  const auto read = round_trip(dir, table);
  CHECK(read.value.table == table);
  const auto column =
      read.value.table.column(core::StationIndex{0}, core::ColumnIndex{0});
  CHECK(std::signbit(column[2].value().value_or(1.0)));
  CHECK(column[3].value() == denormal);
}

TEST_CASE("round trip: strings are bytes, kept exactly",
          "[io][station_nc][read][roundtrip][strings]") {
  const ScratchDir dir;
  const std::vector<std::pair<std::string, std::string>> stations{
      {"x", "1"},
      {"\xC3\xA9t\xC3\xA9", "\xE6\xB0\xB4\xE4\xBD\x8D"},
      {"\xF0\x9F\x8C\x8A", "wave \xF0\x9F\x8C\x8A"},
      {"q\"uote\\", " leading and trailing space "},
      {"USGS-07374000", std::string(1000, 'n')},
  };
  std::vector<core::StationRow> rows;
  rows.reserve(stations.size());
  for (const auto& [id, name] : stations) {
    rows.push_back({.station = station(id, name), .axis = 0});
  }
  const std::size_t n = rows.size();
  const auto table = mov::test::snc::table(
      {{.meta = meta(Quantity::water_level, "wl", "m"),
        .per_station = std::vector<core::Column>(n, {v(1)})}},
      {{t(0)}}, std::move(rows));
  CHECK(round_trip(dir, table).value.table == table);
}

TEST_CASE("round trip: sources, including none and every token",
          "[io][station_nc][read][roundtrip]") {
  const ScratchDir dir;
  std::vector<core::StationRow> rows;
  for (std::size_t s = 0; s <= static_cast<std::size_t>(core::DataSource::user);
       ++s) {
    rows.push_back({.station = station("S" + std::to_string(s), "n", 0, 0,
                                       static_cast<core::DataSource>(s)),
                    .axis = 0});
  }
  rows.push_back(
      {.station = station("none", "n", 0, 0, std::nullopt), .axis = 0});
  const std::size_t n = rows.size();
  const auto table = mov::test::snc::table(
      {{.meta = meta(Quantity::water_level, "wl", "m"),
        .per_station = std::vector<core::Column>(n, {v(1)})}},
      {{t(0)}}, std::move(rows));
  CHECK(round_trip(dir, table).value.table == table);
}

TEST_CASE("round trip: every unit a generic column can have",
          "[io][station_nc][read][roundtrip][units]") {
  const ScratchDir dir;
  const std::vector<std::string> units{
      "m",    "ft",   "in",    "km",   "mi",   "nmi",     "m/s",
      "ft/s", "kt",   "mph",   "km/h", "Pa",   "hPa",     "mb",
      "mH2O", "m3/s", "ft3/s", "degC", "degF", "percent", "degree",
      "1e-3", "psu",  "S m-1", "s"};
  std::vector<core::Variable> variables;
  variables.reserve(units.size() + 1);
  for (std::size_t k = 0; k < units.size(); ++k) {
    variables.push_back({.meta = meta(generic("g" + std::to_string(k)),
                                      "label " + units[k], units[k]),
                         .per_station = {{v(static_cast<double>(k))}}});
  }
  variables.push_back(
      {.meta = meta(generic("no_unit"), "none"), .per_station = {{v(1)}}});
  const auto table = mov::test::snc::table(
      std::move(variables), {{t(0)}}, {{.station = station("A"), .axis = 0}});
  CHECK(round_trip(dir, table).value.table == table);
}

TEST_CASE("round trip: datums on water levels and generic columns",
          "[io][station_nc][read][roundtrip]") {
  const ScratchDir dir;
  std::vector<core::Variable> variables{
      {.meta = meta(Quantity::water_level_prediction, "tide", "m",
                    VerticalDatum::mhhw),
       .per_station = {{v(1)}}},
      {.meta = meta(Quantity::difference, "residual", "m"),
       .per_station = {{v(1)}}}};
  for (const VerticalDatum d : all_datums) {
    variables.push_back({.meta = meta(generic(std::string{"v_"} +
                                              std::string{core::to_string(d)}),
                                      "x", "ft", d),
                         .per_station = {{v(2)}}});
  }
  const auto table = mov::test::snc::table(
      std::move(variables), {{t(0)}}, {{.station = station("A"), .axis = 0}});
  CHECK(round_trip(dir, table).value.table == table);
}

// ---- random tables
// -----------------------------------------------------------

class RandomTables {
 public:
  explicit RandomTables(std::uint64_t seed) : rng_{seed} {}

  StationTable next() {
    const std::size_t stations = pick(1, 5);
    const bool shared = coin();
    std::vector<core::TimeAxis> axes;
    std::vector<core::StationRow> rows;
    for (std::size_t s = 0; s < stations; ++s) {
      if (s == 0 or not shared) {
        axes.push_back(axis(s == 0 ? std::size_t{1} : std::size_t{0}));
      }
      rows.push_back({.station = random_station(s), .axis = shared ? 0 : s});
    }
    std::vector<core::Variable> variables;
    for (const core::SeriesMeta& m : schema()) {
      std::vector<core::Column> per_station;
      per_station.reserve(rows.size());
      for (const core::StationRow& row : rows) {
        per_station.push_back(column(axes[row.axis].size()));
      }
      variables.push_back({.meta = m, .per_station = std::move(per_station)});
    }
    return table(std::move(variables), std::move(axes), std::move(rows));
  }

 private:
  std::size_t pick(std::size_t lo, std::size_t hi) {
    return std::uniform_int_distribution<std::size_t>{lo, hi}(rng_);
  }
  bool coin() { return pick(0, 1) == 1; }

  core::TimeAxis axis(std::size_t at_least) {
    const std::size_t n = pick(at_least, 40);
    std::int64_t now = coin() ? -max_ms + static_cast<std::int64_t>(pick(0, 9))
                              : static_cast<std::int64_t>(pick(0, 1u << 30)) -
                                    (std::int64_t{1} << 29);
    core::TimeAxis out;
    for (std::size_t i = 0; i < n and now <= max_ms; ++i) {
      out.push_back(ms(now));
      const std::int64_t step =
          coin() ? static_cast<std::int64_t>(pick(1, 1000))
                 : static_cast<std::int64_t>(pick(1, std::size_t{1} << 40));
      if (now > max_ms - step) {
        break;
      }
      now += step;
    }
    return out;
  }

  std::string text(std::size_t max_pieces) {
    static const std::vector<std::string> pieces{"a",
                                                 "Z",
                                                 "0",
                                                 " ",
                                                 "_",
                                                 "-",
                                                 ".",
                                                 "\"",
                                                 "\\",
                                                 "\xC3\xA9",
                                                 "\xE6\xB0\xB4",
                                                 "\xF0\x9F\x8C\x8A"};
    std::string out;
    const std::size_t n = pick(1, max_pieces);
    for (std::size_t i = 0; i < n; ++i) {
      out += pieces[pick(0, pieces.size() - 1)];
    }
    return out;
  }

  core::FileStation random_station(std::size_t s) {
    const double lat = std::uniform_real_distribution<double>{-90, 90}(rng_);
    const double lon =
        std::uniform_real_distribution<double>{-179.9, 180}(rng_);
    const std::optional<core::DataSource> source =
        coin() ? std::optional{static_cast<core::DataSource>(pick(0, 6))}
               : std::nullopt;
    return station("S" + std::to_string(s) + text(3), text(12), lat, lon,
                   source);
  }

  std::vector<core::SeriesMeta> schema() {
    std::vector<core::SeriesMeta> out;
    const std::size_t columns = pick(1, 4);
    for (std::size_t k = 0; k < columns; ++k) {
      const auto q = static_cast<Quantity>(
          pick(0, static_cast<std::size_t>(Quantity::difference)));
      if (coin()) {
        out.push_back(registry_meta(q));
      } else {
        out.push_back(generic_meta(k));
      }
    }
    // Unique tokens: drop repeats.
    std::vector<core::SeriesMeta> unique;
    for (const core::SeriesMeta& m : out) {
      bool seen = false;
      for (const core::SeriesMeta& u : unique) {
        seen = seen or core::token(u.quantity()) == core::token(m.quantity());
      }
      if (not seen) {
        unique.push_back(m);
      }
    }
    return unique;
  }

  core::SeriesMeta registry_meta(Quantity q) {
    const std::string_view canonical = core::info(q).canonical_unit;
    const std::optional<std::string_view> unit_text =
        canonical.empty() ? std::optional<std::string_view>{"ft"}
                          : std::optional{canonical};
    std::optional<VerticalDatum> datum;
    if (core::datum_applicable(core::QuantityId{q}) and coin()) {
      datum = all_datums[pick(0, all_datums.size() - 1)];
    }
    return meta(q, text(6), unit_text, datum);
  }

  core::SeriesMeta generic_meta(std::size_t k) {
    static const std::vector<std::string_view> units{"m", "knot", "degF", "psu",
                                                     "1e-3"};
    const std::optional<std::string_view> unit_text =
        coin() ? std::optional{units[pick(0, units.size() - 1)]} : std::nullopt;
    std::optional<VerticalDatum> datum;
    if (coin()) {
      datum = all_datums[pick(0, all_datums.size() - 1)];
    }
    const std::string token =
        k == 0 and coin() ? "value" : "q" + std::to_string(k);
    return meta(token == "value"
                    ? core::GenericQuantity::value()
                    : generic(token, coin() ? "sea_water_salinity" : ""),
                text(6), unit_text, datum);
  }

  Sample sample() {
    switch (pick(0, 9)) {
      case 0:
        return missing;
      case 1:
        return dry;
      case 2:
        return v(coin() ? DBL_MAX : -DBL_MAX);
      case 3:
        return v(std::numeric_limits<double>::denorm_min());
      default:
        return v(std::uniform_real_distribution<double>{-1e6, 1e6}(rng_));
    }
  }

  core::Column column(std::size_t n) {
    core::Column out;
    for (std::size_t i = 0; i < n; ++i) {
      out.push_back(sample());
    }
    return out;
  }

  std::mt19937_64 rng_;
};

TEST_CASE("round trip: random tables of both layouts",
          "[io][station_nc][read][roundtrip][property]") {
  const ScratchDir dir;
  RandomTables tables{20261007};
  std::size_t orthogonal = 0;
  std::size_t incomplete = 0;
  for (int round = 0; round < 60; ++round) {
    CAPTURE(round);
    const StationTable table = tables.next();
    const auto read = round_trip(dir, table);
    CHECK(read.value.table == table);
    (layout_of(read.value) == StationNcLayout::orthogonal ? orthogonal
                                                          : incomplete) += 1;
  }
  CHECK(orthogonal > 10);
  CHECK(incomplete > 10);
}

// ---- normalizations
// ------------------------------------------------------------

TEST_CASE("what the writer normalizes reads back normalized",
          "[io][station_nc][read]") {
  const ScratchDir dir;
  core::FileStation unnamed = station("8761724", "");
  unnamed.native =
      core::NativePoint::make({.x = 1, .y = 2}, core::Epsg::wgs84()).value();
  const auto table =
      mov::test::snc::table({{.meta = meta(Quantity::water_level, "", "ft"),
                              .per_station = {{v(10)}}}},
                            {{t(0)}}, {{.station = unnamed, .axis = 0}});
  const auto path = dir / "n.nc";
  static_cast<void>(must_write(path, table));
  const auto read = must_read(read_all(path));
  const StationTable& back = read.value.table;
  CHECK(back.station(core::StationIndex{0}).name.view() == "Station 8761724");
  CHECK(not back.station(core::StationIndex{0}).native);
  CHECK(back.schema()[0].label() == "Water level");
  CHECK(back.schema()[0].unit() == unit("m"));
  CHECK(back.column(core::StationIndex{0}, core::ColumnIndex{0})[0].value() ==
        10 * 0.3048);
}

// ---- selections and the catalog
// --------------------------------------------------

TEST_CASE("a selection reads those stations in its order",
          "[io][station_nc][read][selection]") {
  const ScratchDir dir;
  for (const auto& c : {station_nc::orthogonal(), station_nc::incomplete()}) {
    CAPTURE(c.name);
    const auto path = station_nc::write(c, dir.path());
    const auto both = core::StationSelection::make({1, 0}, 2).value();
    const auto read = must_read(io::read_station_netcdf(path, both, {}));
    const StationTable& t = read.value.table;
    REQUIRE(t.size() == 2);
    CHECK(t.station(core::StationIndex{0}) ==
          c.table.station(core::StationIndex{1}));
    CHECK(t.series(core::StationIndex{0}, core::ColumnIndex{0}) ==
          c.table.series(core::StationIndex{1}, core::ColumnIndex{0}));
    CHECK(t.series(core::StationIndex{1}, core::ColumnIndex{1}) ==
          c.table.series(core::StationIndex{0}, core::ColumnIndex{1}));

    const auto second = core::StationSelection::make({1}, 2).value();
    const auto one = must_read(io::read_station_netcdf(path, second, {}));
    REQUIRE(one.value.table.size() == 1);
    CHECK(one.value.table.series(core::StationIndex{0}, core::ColumnIndex{0}) ==
          c.table.series(core::StationIndex{1}, core::ColumnIndex{0}));

    const auto none = core::StationSelection::make({}, 2).value();
    const auto empty = must_read(io::read_station_netcdf(path, none, {}));
    CHECK(empty.value.table.size() == 0);
    CHECK(empty.value.table.schema().size() == 2);

    const auto wrong = core::StationSelection::all(3);
    CHECK(format_error_of(io::read_station_netcdf(path, wrong, {})).code ==
          FormatErrc::station_count_mismatch);
    reopens(path);
  }
}

TEST_CASE("selected stations far apart in a chunked file",
          "[io][station_nc][read][selection]") {
  const ScratchDir dir;
  std::vector<core::TimeAxis> axes;
  std::vector<core::Column> columns;
  for (std::size_t s = 0; s < 400; ++s) {
    axes.push_back({t(0), t(static_cast<std::int64_t>(s % 3) + 1)});
    columns.push_back({v(static_cast<double>(s)), dry});
  }
  const auto table = water_levels(axes, columns);
  const auto path = dir / "many.nc";
  static_cast<void>(must_write(path, table));
  const auto which =
      core::StationSelection::make({399, 0, 200, 37}, 400).value();
  const auto read = must_read(io::read_station_netcdf(path, which, {}));
  REQUIRE(read.value.table.size() == 4);
  std::size_t k = 0;
  for (const std::size_t s :
       {std::size_t{399}, std::size_t{0}, std::size_t{200}, std::size_t{37}}) {
    CHECK(
        read.value.table.series(core::StationIndex{k}, core::ColumnIndex{0}) ==
        table.series(core::StationIndex{s}, core::ColumnIndex{0}));
    ++k;
  }
}

TEST_CASE("inspect: stations, sample counts and schema without the samples",
          "[io][station_nc][read][catalog]") {
  const ScratchDir dir;
  const auto c = station_nc::incomplete();
  const auto path = station_nc::write(c, dir.path());
  const auto catalog = io::inspect_station_netcdf(path, {});
  REQUIRE(catalog.has_value());
  CHECK(catalog->warnings.empty());
  CHECK(catalog->value.origin == io::StationFileOrigin{io::V5Origin{
                                     .version = {.major = 1, .minor = 0},
                                     .layout = StationNcLayout::incomplete}});
  REQUIRE(catalog->value.stations.size() == 2);
  CHECK(catalog->value.stations[1] ==
        io::CatalogStation{.station = c.table.station(core::StationIndex{1}),
                           .samples = 5});
  CHECK(catalog->value.stations[0].samples == 3);
  CHECK(catalog->value.schema ==
        std::vector<core::SeriesMeta>(c.table.schema().begin(),
                                      c.table.schema().end()));
  reopens(path);

  const auto o = station_nc::write(station_nc::orthogonal(), dir.path());
  const auto ortho = io::inspect_station_netcdf(o, {});
  REQUIRE(ortho.has_value());
  CHECK(std::get<io::V5Origin>(ortho->value.origin).layout ==
        StationNcLayout::orthogonal);
  CHECK(ortho->value.stations[0].samples == 4);
  CHECK(ortho->value.stations[1].samples == 4);
}

TEST_CASE("limits and cancellation", "[io][station_nc][read][limits]") {
  const ScratchDir dir;
  const auto c = station_nc::orthogonal();
  const auto path = station_nc::write(c, dir.path());
  SECTION("more samples than max_elements") {
    io::ReadContext ctx;
    ctx.limits.max_elements = 15;  // 2 stations x 4 times x 2 columns = 16
    const auto read = read_all(path, ctx);
    REQUIRE(not read.has_value());
    const auto* nc = std::get_if<io::NcError>(&read.error());
    REQUIRE(nc != nullptr);
    CHECK(nc->status == io::NcStatus{io::WrapperFault::too_large});
  }
  SECTION("a stop request") {
    io::ReadContext ctx;
    ctx.stop = io::StopToken{[] { return true; }};
    const auto read = read_all(path, ctx);
    REQUIRE(not read.has_value());
    CHECK(std::holds_alternative<io::Cancelled>(read.error()));
  }
  reopens(path);
}

TEST_CASE("not a station file", "[io][station_nc][read]") {
  const ScratchDir dir;
  SECTION("no file") {
    const auto read = read_all(dir / "missing.nc");
    REQUIRE(not read.has_value());
    CHECK(std::holds_alternative<io::NcError>(read.error()));
  }
  SECTION("not netCDF") {
    mov::test::write_bytes(dir / "text.nc", "just text\n");
    const auto read = read_all(dir / "text.nc");
    REQUIRE(not read.has_value());
    CHECK(std::holds_alternative<io::NcError>(read.error()));
    CHECK(not io::inspect_station_netcdf(dir / "text.nc", {}).has_value());
  }
}

TEST_CASE("parse_station_nc_version", "[io][station_nc]") {
  using io::parse_station_nc_version;
  CHECK(parse_station_nc_version("1.0") ==
        io::StationNcVersion{.major = 1, .minor = 0});
  CHECK(parse_station_nc_version("12.3456") ==
        io::StationNcVersion{.major = 12, .minor = 3456});
  for (const char* bad : {"", "1", "1.", ".1", "1.0.0", "01.0", "1.00", "a.b",
                          "1.-1", " 1.0", "1.0 ", "12345.0", "+1.0"}) {
    CAPTURE(bad);
    CHECK(not parse_station_nc_version(bad));
  }
}

// ---- what an incomplete read costs ------------------------------------------

/// One long station among many short ones: obs is 10000, every other station
/// has one sample, so a read of the short ones that reads whole rows would
/// read 10000 elements per station for one sample each.
StationTable pathological() {
  std::vector<core::TimeAxis> axes;
  std::vector<core::Column> columns;
  core::TimeAxis long_axis;
  core::Column long_column;
  for (std::int64_t k = 0; k < 10000; ++k) {
    long_axis.push_back(t(k));
    long_column.push_back(v(static_cast<double>(k)));
  }
  axes.push_back(std::move(long_axis));
  columns.push_back(std::move(long_column));
  for (std::int64_t s = 1; s < 50; ++s) {
    axes.push_back({t(s)});
    columns.push_back({v(static_cast<double>(s))});
  }
  return water_levels(std::move(axes), std::move(columns));
}

TEST_CASE("an incomplete read costs what the samples cost, not obs (B1)",
          "[io][station_nc][read][padding][regression]") {
  const ScratchDir dir;
  const StationTable table = pathological();
  const auto path = dir / "pathological.nc";
  static_cast<void>(must_write(path, table));
  std::vector<std::size_t> short_ones(49);
  std::ranges::copy(std::views::iota(std::size_t{1}, std::size_t{50}),
                    short_ones.begin());
  const auto which = core::StationSelection::make(short_ones, 50).value();
  // A bound on what is read: every read (and every block of one) is held to
  // max_elements. The short stations hold 49 samples; their whole rows hold
  // 490000 elements.
  io::ReadContext ctx;
  ctx.limits.max_elements = 1000;

  SECTION("the default reads each group's samples and one padding element") {
    const auto read = must_read(io::read_station_netcdf(path, which, ctx));
    REQUIRE(read.value.table.size() == 49);
    for (std::size_t p = 0; p < 49; ++p) {
      CHECK(read.value.table.series(core::StationIndex{p},
                                    core::ColumnIndex{0}) ==
            table.series(core::StationIndex{p + 1}, core::ColumnIndex{0}));
    }
  }
  SECTION("the whole padding is opt-in and charged as selected x obs") {
    const auto read = io::read_station_netcdf(
        path, which, ctx, {.padding = io::PaddingCheck::whole});
    REQUIRE(not read.has_value());
    const auto* nc = std::get_if<io::NcError>(&read.error());
    REQUIRE(nc != nullptr);
    CHECK(nc->status == io::NcStatus{io::WrapperFault::too_large});
  }
}

TEST_CASE("PaddingCheck::whole sees padding the boundary check does not read",
          "[io][station_nc][read][padding]") {
  const ScratchDir dir;
  const auto path = dir / "pathological.nc";
  static_cast<void>(must_write(path, pathological()));
  {
    mov::test::ncgen::Editor edit{path};
    edit.put("water_level", {7, 5}, 1.0);  // station 7 has one sample
  }
  const auto which = core::StationSelection::make({7, 8}, 50).value();
  CHECK(io::read_station_netcdf(path, which, {}).has_value());
  const auto e = format_error_of(io::read_station_netcdf(
      path, which, {}, {.padding = io::PaddingCheck::whole}));
  CHECK(e.code == FormatErrc::padding_not_missing);
  CHECK(e.station == 7);
  CHECK(e.index == 5);
  CHECK(
      format_error_of(read_all(path, {}, {.padding = io::PaddingCheck::whole}))
          .code == FormatErrc::padding_not_missing);
}

// ---- the idempotence law ----------------------------------------------------

TEST_CASE("re-writing a read-back table gives no writer warnings",
          "[io][station_nc][read][roundtrip]") {
  const ScratchDir dir;
  core::FileStation unnamed = station("A", "");
  unnamed.native =
      core::NativePoint::make({.x = 1, .y = 2}, core::Epsg::wgs84()).value();
  const auto table = mov::test::snc::table(
      {{.meta = meta(Quantity::water_level, "", "ft"),
        .per_station = {{v(10), dry, missing}}},
       {.meta = meta(Quantity::air_temperature, "air", "degF"),
        .per_station = {{v(50), v(51), v(52)}}}},
      {{t(0), t(1), t(2)}}, {{.station = unnamed, .axis = 0}});
  const auto first = must_write(dir / "a.nc", table);
  CHECK(not first.warnings.empty());
  const auto once = must_read(read_all(dir / "a.nc"));
  const auto second = must_write(dir / "b.nc", once.value.table);
  CHECK(second.warnings.empty());
  CHECK(must_read(read_all(dir / "b.nc")).value.table == once.value.table);
}

}  // namespace
