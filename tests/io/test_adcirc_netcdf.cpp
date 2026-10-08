// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "adcirc_test_support.hpp"
#include "model_fixtures.hpp"
#include "model_nc_support.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/core/vector_series.hpp"
#include "mov/io/adcirc_ascii.hpp"
#include "mov/io/adcirc_netcdf.hpp"
#include "mov/io/detail/adcirc_schema.hpp"
#include "mov/io/detail/station_groups.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/projection.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"

namespace {

using mov::core::ColumnIndex;
using mov::core::Dry;
using mov::core::Epsg;
using mov::core::Missing;
using mov::core::Sample;
using mov::core::StationIndex;
using mov::core::StationSelection;
using mov::core::StationTable;
using mov::core::Time;
using mov::io::AdcircKind;
using mov::io::AdcircNcRequest;
using mov::io::Cancelled;
using mov::io::FormatErrc;
using mov::io::ReadContext;
using mov::io::StopToken;
using mov::io::WarningCode;
using mov::test::at_seconds;
using mov::test::cold_start;
using mov::test::format_error_of;
using mov::test::nc_error_in;
using mov::test::number;
using mov::test::sample;
using mov::test::samples_of;
using mov::test::warning_count;
using mov::test::ncgen::AdcircNc;
using mov::test::ncgen::DataType;
using mov::test::ncgen::TimeType;
using namespace std::string_literals;

constexpr double not_a_number = std::numeric_limits<double>::quiet_NaN();

// netCDF-C's default fill values (NC_FILL_DOUBLE / NC_FILL_FLOAT).
constexpr double default_fill = 9.9692099683868690e+36;

AdcircNcRequest request(AdcircKind kind, StationSelection stations,
                        std::optional<Time> start = cold_start(),
                        Epsg crs = Epsg::wgs84()) {
  return {.kind = kind,
          .cold_start = start,
          .crs = crs,
          .stations = std::move(stations)};
}

StationSelection everything(std::size_t n) { return StationSelection::all(n); }

StationSelection select(std::vector<std::size_t> indices, std::size_t of) {
  auto made = StationSelection::make(std::move(indices), of);
  REQUIRE(made.has_value());
  return *made;
}

// The table of a successful read, or the failure of the test.
mov::io::Read<StationTable> read_ok(const std::filesystem::path& path,
                                    const AdcircNcRequest& req,
                                    const ReadContext& ctx = {}) {
  auto read = mov::io::read_adcirc_netcdf(path, req, ctx);
  INFO((read ? std::string{} : mov::test::what(read.error())));
  REQUIRE(read.has_value());
  return *std::move(read);
}

// 100 c + 10 s + t / 4, the generator's default.
double plain(std::size_t t, std::size_t s, std::size_t c) {
  return 100.0 * static_cast<double>(c) + 10.0 * static_cast<double>(s) +
         0.25 * static_cast<double>(t);
}

AdcircNc zeta_spec() {
  AdcircNc spec;
  spec.variables = {"zeta"};
  return spec;
}

AdcircNc velocity_spec() {
  AdcircNc spec;
  spec.variables = {"u-vel", "v-vel"};
  return spec;
}

}  // namespace

// ---- the values
// ---------------------------------------------------------------

TEST_CASE("elevation: fill and -999 are Dry, -998.99 and -950 are values",
          "[io][adcirc][netcdf]") {
  const mov::test::ScratchDir dir;
  AdcircNc spec = zeta_spec();
  spec.value = [](std::size_t t, std::size_t s, std::size_t c) {
    if (s == 0) {
      constexpr std::array<double, 5> column{-99999.0, -999.0, -998.99, -950.0,
                                             not_a_number};
      return column.at(t);
    }
    return plain(t, s, c);
  };
  make_adcirc_nc(dir / "fort.61.nc", spec);

  const auto read = read_ok(dir / "fort.61.nc",
                            request(AdcircKind::elevation, everything(3)));
  const StationTable& table = read.value;
  REQUIRE(table.size() == 3);
  CHECK(samples_of(table, 0, 0) ==
        std::vector<Sample>{Dry{}, Dry{}, sample(-998.99), sample(-950.0),
                            Missing{}});
  CHECK(samples_of(table, 1, 0) ==
        std::vector<Sample>{sample(10.0), sample(10.25), sample(10.5),
                            sample(10.75), sample(11.0)});
  // The NaN is counted; the fill and the -999 are no warning (Dry is a value).
  CHECK(warning_count(read.warnings, WarningCode::nonfinite_masked) == 1);
  CHECK(warning_count(read.warnings, WarningCode::epoch_used) == 0);

  // One shared axis, 600 s apart from the cold start.
  REQUIRE(table.schema().size() == 1);
  CHECK(mov::core::token(table.schema()[0].quantity()) == "water_level");
  CHECK(table.schema()[0].unit() ==
        std::optional<mov::core::Unit>{mov::core::LengthUnit::meter});
  const auto times = table.times(StationIndex{0});
  REQUIRE(times.size() == 5);
  for (std::size_t t = 0; t < 5; ++t) {
    CHECK(times[t] == at_seconds(600.0 * static_cast<double>(t + 1)));
  }
  CHECK(table.single_axis());
}

TEST_CASE("the stations: ids, default names, positions, source",
          "[io][adcirc][netcdf]") {
  const mov::test::ScratchDir dir;
  make_adcirc_nc(dir / "fort.61.nc", zeta_spec());
  const auto read = read_ok(dir / "fort.61.nc",
                            request(AdcircKind::elevation, everything(3)));
  for (std::size_t s = 0; s < 3; ++s) {
    const auto& station = read.value.station(StationIndex{s});
    CHECK(station.id.view() == std::to_string(s));
    CHECK(station.name.view() == "Station " + std::to_string(s));
    CHECK(station.location.lon() == -90.0 - 0.5 * static_cast<double>(s));
    CHECK(station.location.lat() == 29.0 - static_cast<double>(s));
    CHECK(station.source == mov::core::DataSource::adcirc);
    CHECK(not(station.native.has_value()));
  }
}

TEST_CASE("float variables read as the float values (B4)",
          "[io][adcirc][netcdf][regression][B4]") {
  // v4 read the fill value into a double through a float variable's
  // nc_inq_var_fill and the data through an untyped nc_get_vara: garbage.
  const mov::test::ScratchDir dir;
  AdcircNc spec = zeta_spec();
  spec.type = DataType::float32;
  spec.value = [](std::size_t t, std::size_t s, std::size_t) {
    if (s == 0 and t == 0) {
      return -99999.0;
    }
    return 0.1 * static_cast<double>(1 + s + t);
  };
  make_adcirc_nc(dir / "fort.61.nc", spec);
  const auto read = read_ok(dir / "fort.61.nc",
                            request(AdcircKind::elevation, everything(3)));
  const auto widen = [](double v) {
    return static_cast<double>(static_cast<float>(v));
  };
  CHECK(samples_of(read.value, 0, 0)[0] == Sample{Dry{}});
  CHECK(samples_of(read.value, 0, 0)[1] == sample(widen(0.1 * 2.0)));
  CHECK(samples_of(read.value, 2, 0)[4] == sample(widen(0.1 * 7.0)));
  // Exactly the float, widened: not the double 0.7.
  CHECK(samples_of(read.value, 2, 0)[4] != sample(0.1 * 7.0));
}

TEST_CASE("the library's default fill is Missing, not Dry",
          "[io][adcirc][netcdf]") {
  // No _FillValue attribute: the fill is netCDF-C's 9.97e36 (B9).
  const mov::test::ScratchDir dir;
  for (const DataType type : {DataType::float64, DataType::float32}) {
    AdcircNc spec = zeta_spec();
    spec.type = type;
    spec.fill = std::nullopt;
    spec.value = [](std::size_t t, std::size_t s, std::size_t c) {
      return (s == 1 and t == 2) ? default_fill : plain(t, s, c);
    };
    make_adcirc_nc(dir / "default.nc", spec);
    const auto read = read_ok(dir / "default.nc",
                              request(AdcircKind::elevation, everything(3)));
    CHECK(samples_of(read.value, 1, 0)[2] == Sample{Missing{}});
    CHECK(samples_of(read.value, 1, 0)[3] == sample(10.75));
  }
}

TEST_CASE("velocity: a fill in either component empties both (N7)",
          "[io][adcirc][netcdf][regression][N7]") {
  const mov::test::ScratchDir dir;
  AdcircNc spec = velocity_spec();
  spec.value = [](std::size_t t, std::size_t s, std::size_t c) {
    if (s != 0) {
      return plain(t, s, c);
    }
    switch (t) {
      case 0:
        return c == 0 ? -99999.0 : 0.5;  // u fill
      case 1:
        return c == 1 ? -99999.0 : 0.5;  // v fill
      case 2:
        return c == 0 ? not_a_number : 0.5;  // u is NaN: only u is lost
      case 3:
        return c == 0 ? 0.049298094833 : 0.012227184139;
      default:
        return c == 0 ? -999.0 : 0.5;  // -999 is fill
    }
  };
  make_adcirc_nc(dir / "fort.62.nc", spec);
  const auto read =
      read_ok(dir / "fort.62.nc", request(AdcircKind::velocity, everything(3)));
  const StationTable& table = read.value;
  REQUIRE(table.schema().size() == 2);
  CHECK(mov::core::token(table.schema()[0].quantity()) == "current_u");
  CHECK(mov::core::token(table.schema()[1].quantity()) == "current_v");
  CHECK(samples_of(table, 0, 0) ==
        std::vector<Sample>{Missing{}, Missing{}, Missing{},
                            sample(0.049298094833), Missing{}});
  CHECK(samples_of(table, 0, 1) ==
        std::vector<Sample>{Missing{}, Missing{}, sample(0.5),
                            sample(0.012227184139), Missing{}});
  // Parity with the ASCII reader's B1 test: the magnitude is the hypotenuse;
  // v4 printed (u^2 + v^2)^2 = 6.66e-6.
  const auto vector = mov::core::vector_series(table, StationIndex{0},
                                               ColumnIndex{0}, ColumnIndex{1});
  REQUIRE(vector.has_value());
  const auto speed = vector->magnitude();
  CHECK(speed.samples()[0] == Sample{Missing{}});
  CHECK(number(speed.samples()[3]) == Catch::Approx(0.0507918).epsilon(1e-6));
  CHECK(warning_count(read.warnings, WarningCode::nonfinite_masked) == 1);
}

TEST_CASE("pressure and wind: values at or below -999 are Missing, not Dry",
          "[io][adcirc][netcdf]") {
  const mov::test::ScratchDir dir;
  AdcircNc pressure;
  pressure.variables = {"pressure"};
  pressure.value = [](std::size_t t, std::size_t s, std::size_t) {
    return (s == 0 and t == 0) ? -99999.0 : 10.0 + static_cast<double>(t);
  };
  make_adcirc_nc(dir / "fort.71.nc", pressure);
  const auto p =
      read_ok(dir / "fort.71.nc", request(AdcircKind::pressure, everything(3)));
  CHECK(samples_of(p.value, 0, 0)[0] == Sample{Missing{}});
  CHECK(samples_of(p.value, 0, 0)[1] == sample(11.0));
  CHECK(mov::core::token(p.value.schema()[0].quantity()) == "air_pressure");

  AdcircNc wind;
  wind.variables = {"windx", "windy"};
  make_adcirc_nc(dir / "fort.72.nc", wind);
  const auto w =
      read_ok(dir / "fort.72.nc", request(AdcircKind::wind, everything(3)));
  CHECK(mov::core::token(w.value.schema()[0].quantity()) == "wind_u");
  CHECK(mov::core::token(w.value.schema()[1].quantity()) == "wind_v");
  CHECK(samples_of(w.value, 1, 1)[2] == sample(110.5));
}

TEST_CASE("packed data and missing_value", "[io][adcirc][netcdf]") {
  const mov::test::ScratchDir dir;
  AdcircNc spec;
  spec.variables = {"pressure"};
  spec.type = DataType::int16;
  spec.fill = -32767;
  spec.scale_factor = 0.5;
  spec.add_offset = 10.0;
  spec.value = [](std::size_t t, std::size_t s, std::size_t) {
    if (s == 0 and t == 1) {
      return -32767.0;
    }
    return 2.0 * static_cast<double>(t) + static_cast<double>(s);
  };
  make_adcirc_nc(dir / "packed.nc", spec);
  const auto read =
      read_ok(dir / "packed.nc", request(AdcircKind::pressure, everything(3)));
  // Stored values are unpacked after masking: stored * 0.5 + 10.
  CHECK(samples_of(read.value, 0, 0) ==
        std::vector<Sample>{sample(10.0), Missing{}, sample(12.0), sample(13.0),
                            sample(14.0)});
  CHECK(samples_of(read.value, 1, 0)[0] == sample(10.5));

  AdcircNc marked;
  marked.variables = {"pressure"};
  marked.missing_value = -1.0;
  marked.value = [](std::size_t t, std::size_t, std::size_t) {
    return t == 2 ? -1.0 : 5.0;
  };
  make_adcirc_nc(dir / "marked.nc", marked);
  const auto m =
      read_ok(dir / "marked.nc", request(AdcircKind::pressure, everything(3)));
  CHECK(samples_of(m.value, 0, 0)[2] == Sample{Missing{}});
  CHECK(samples_of(m.value, 0, 0)[1] == sample(5.0));
}

TEST_CASE("integer data of every width is unpacked after masking",
          "[io][adcirc][netcdf]") {
  struct Width {
    DataType type;
    double fill;
  };
  const mov::test::ScratchDir dir;
  for (const Width w : {Width{.type = DataType::int8, .fill = -100.0},
                        Width{.type = DataType::int16, .fill = -32767.0},
                        Width{.type = DataType::int32, .fill = -99999.0}}) {
    AdcircNc spec;
    spec.variables = {"pressure"};
    spec.type = w.type;
    spec.fill = w.fill;
    spec.scale_factor = 0.5;
    spec.add_offset = 10.0;
    spec.value = [fill = w.fill](std::size_t t, std::size_t s, std::size_t) {
      return (s == 0 and t == 1) ? fill : static_cast<double>(t);
    };
    make_adcirc_nc(dir / "ints.nc", spec);
    const auto read =
        read_ok(dir / "ints.nc", request(AdcircKind::pressure, everything(3)));
    CHECK(samples_of(read.value, 0, 0) ==
          std::vector<Sample>{sample(10.0), Missing{}, sample(11.0),
                              sample(11.5), sample(12.0)});
    CHECK(samples_of(read.value, 1, 0)[1] == sample(10.5));
  }
}

TEST_CASE("data that is 64-bit or unsigned is refused",
          "[io][adcirc][netcdf]") {
  // No silent all-Missing column: a double cannot hold every 64-bit value,
  // and the wrapper reads no unsigned types.
  const mov::test::ScratchDir dir;
  for (const DataType type : {DataType::int64, DataType::uint8}) {
    AdcircNc spec = zeta_spec();
    spec.type = type;
    spec.fill = std::nullopt;
    make_adcirc_nc(dir / "refused.nc", spec);
    const auto read = mov::io::read_adcirc_netcdf(
        dir / "refused.nc", request(AdcircKind::elevation, everything(3)), {});
    REQUIRE(not(read.has_value()));
    const auto* error = nc_error_in(read.error());
    REQUIRE(error != nullptr);
    CHECK(error->status ==
          mov::io::NcStatus{mov::io::WrapperFault::type_mismatch});
    CHECK(error->object == "zeta");
  }
}

// ---- station names
// ----------------------------------------------------------------

TEST_CASE(
    "station_name: cut at the first NUL, simplified, stride from the file",
    "[io][adcirc][netcdf][regression][B11]") {
  // The legacy fixtures have name_len 50; v4's DFlow reader hard-coded 200.
  const mov::test::ScratchDir dir;
  for (const std::size_t name_len : {10U, 50U, 300U}) {
    AdcircNc spec = zeta_spec();
    spec.name_len = name_len;
    spec.station_names = {"Alpha"s + '\0' + "junk", " B  C "s, ""s};
    make_adcirc_nc(dir / "names.nc", spec);
    const auto read = read_ok(dir / "names.nc",
                              request(AdcircKind::elevation, everything(3)));
    CHECK(read.value.station(StationIndex{0}).name.view() == "Alpha");
    CHECK(read.value.station(StationIndex{1}).name.view() == "B C");
    CHECK(read.value.station(StationIndex{2}).name.view() == "Station 2");
    CHECK(read.warnings.empty());
  }
}

TEST_CASE("station_name: bytes that are not UTF-8 are replaced, with a warning",
          "[io][adcirc][netcdf]") {
  const mov::test::ScratchDir dir;
  AdcircNc spec = zeta_spec();
  spec.station_names = {"ab\xE9"s, "ok"s, "x\xFF\xFE"s};
  make_adcirc_nc(dir / "names.nc", spec);
  const auto read =
      read_ok(dir / "names.nc", request(AdcircKind::elevation, everything(3)));
  CHECK(read.value.station(StationIndex{0}).name.view() == "ab\xEF\xBF\xBD");
  CHECK(read.value.station(StationIndex{1}).name.view() == "ok");
  CHECK(warning_count(read.warnings, WarningCode::invalid_utf8_replaced) == 2);
}

// ---- which file is it
// -------------------------------------------------------------------

TEST_CASE("kinds: partner variables and the files that are not ADCIRC's",
          "[io][adcirc][netcdf]") {
  const mov::test::ScratchDir dir;
  const auto inspect = [&](const std::string& name) {
    return mov::io::inspect_adcirc_netcdf(dir / name, Epsg::wgs84(), {});
  };
  SECTION("u-vel without v-vel") {
    AdcircNc spec;
    spec.variables = {"u-vel"};
    make_adcirc_nc(dir / "a.nc", spec);
    CHECK(format_error_of(inspect("a.nc")).code ==
          FormatErrc::partner_variable_missing);
    const auto read = mov::io::read_adcirc_netcdf(
        dir / "a.nc", request(AdcircKind::velocity, everything(3)), {});
    CHECK(format_error_of(read).code == FormatErrc::partner_variable_missing);
    CHECK(format_error_of(read).subject == "v-vel");
  }
  SECTION("windx without windy") {
    AdcircNc spec;
    spec.variables = {"windx"};
    make_adcirc_nc(dir / "a.nc", spec);
    CHECK(format_error_of(inspect("a.nc")).code ==
          FormatErrc::partner_variable_missing);
  }
  SECTION("a lone v-vel is another kind of file") {
    AdcircNc spec;
    spec.variables = {"v-vel"};
    make_adcirc_nc(dir / "a.nc", spec);
    CHECK(format_error_of(inspect("a.nc")).code == FormatErrc::not_this_format);
  }
  SECTION("no data variable") {
    AdcircNc spec;
    spec.variables = {"salinity"};
    make_adcirc_nc(dir / "a.nc", spec);
    CHECK(format_error_of(inspect("a.nc")).code ==
          FormatErrc::missing_variable);
  }
  SECTION("the request names a kind the file does not have") {
    make_adcirc_nc(dir / "a.nc", zeta_spec());
    const auto read = mov::io::read_adcirc_netcdf(
        dir / "a.nc", request(AdcircKind::pressure, everything(3)), {});
    CHECK(format_error_of(read).code == FormatErrc::missing_variable);
    CHECK(format_error_of(read).subject == "pressure");
  }
  SECTION("no model attribute, or another model") {
    AdcircNc spec = zeta_spec();
    spec.model = std::nullopt;
    make_adcirc_nc(dir / "a.nc", spec);
    CHECK(format_error_of(inspect("a.nc")).code == FormatErrc::not_this_format);
    spec.model = "FVCOM";
    make_adcirc_nc(dir / "b.nc", spec);
    CHECK(format_error_of(inspect("b.nc")).code == FormatErrc::not_this_format);
    const auto read = mov::io::read_adcirc_netcdf(
        dir / "b.nc", request(AdcircKind::elevation, everything(3)), {});
    CHECK(format_error_of(read).code == FormatErrc::not_this_format);
  }
  SECTION("missing dimensions and variables") {
    AdcircNc spec = zeta_spec();
    spec.write_coordinates = false;
    make_adcirc_nc(dir / "a.nc", spec);
    const auto result = inspect("a.nc");
    CHECK(format_error_of(result).code == FormatErrc::missing_variable);
    CHECK(format_error_of(result).subject == "x");
    spec.write_coordinates = true;
    spec.write_time = false;
    make_adcirc_nc(dir / "b.nc", spec);
    const auto no_time = inspect("b.nc");
    CHECK(format_error_of(no_time).code == FormatErrc::missing_variable);
    CHECK(format_error_of(no_time).subject == "time");
  }
  SECTION("a file that is not netCDF, and one that is not there") {
    mov::test::write_bytes(dir / "text.nc", "not netCDF");
    const auto text = inspect("text.nc");
    REQUIRE(not(text.has_value()));
    CHECK(nc_error_in(text.error()) != nullptr);
    const auto absent = inspect("absent.nc");
    REQUIRE(not(absent.has_value()));
    CHECK(nc_error_in(absent.error()) != nullptr);
  }
}

TEST_CASE(
    "dimensions are found by name and variables matched by identity (B12)",
    "[io][adcirc][netcdf][regression][B12]") {
  // v4 mapped a missing name to dimension 0 and compared positions.
  const mov::test::ScratchDir dir;
  SECTION("`time` is not dimension 0") {
    AdcircNc spec = zeta_spec();
    spec.station_dim_first = true;
    make_adcirc_nc(dir / "a.nc", spec);
    const auto read =
        read_ok(dir / "a.nc", request(AdcircKind::elevation, everything(3)));
    CHECK(samples_of(read.value, 2, 0)[4] == sample(21.0));
    CHECK(read.value.times(StationIndex{0}).size() == 5);
  }
  SECTION("data over (station, time) is not (time, station)") {
    AdcircNc spec = zeta_spec();
    spec.transposed_data = true;
    make_adcirc_nc(dir / "a.nc", spec);
    const auto read = mov::io::read_adcirc_netcdf(
        dir / "a.nc", request(AdcircKind::elevation, everything(3)), {});
    CHECK(format_error_of(read).code == FormatErrc::dimension_mismatch);
    CHECK(format_error_of(read).subject == "zeta");
  }
}

// ---- the clock
// ----------------------------------------------------------------------------

TEST_CASE("cold start: given, or the epoch of time:units with a warning",
          "[io][adcirc][netcdf]") {
  const mov::test::ScratchDir dir;
  AdcircNc spec = zeta_spec();
  spec.time_units = "seconds since 2011-02-03 04:05:06";
  make_adcirc_nc(dir / "a.nc", spec);
  const Time epoch =
      mov::core::parse_utc_datetime("2011-02-03 04:05:06").value();

  SECTION("no cold start: the file's epoch, and the warning says so") {
    const auto read =
        read_ok(dir / "a.nc",
                request(AdcircKind::elevation, everything(3), std::nullopt));
    CHECK(read.value.times(StationIndex{0})[0] ==
          epoch + std::chrono::seconds{600});
    const auto* warning =
        mov::test::find_warning(read.warnings, WarningCode::epoch_used);
    REQUIRE(warning != nullptr);
    CHECK(warning->subject == "seconds since 2011-02-03 04:05:06");
  }
  SECTION("a cold start wins, and the warning says the file disagrees") {
    const auto read =
        read_ok(dir / "a.nc", request(AdcircKind::elevation, everything(3)));
    CHECK(read.value.times(StationIndex{0})[0] == at_seconds(600.0));
    CHECK(warning_count(read.warnings, WarningCode::epoch_used) == 0);
    const auto* differs =
        mov::test::find_warning(read.warnings, WarningCode::cold_start_differs);
    REQUIRE(differs != nullptr);
    CHECK(differs->subject == "seconds since 2011-02-03 04:05:06");
  }
  SECTION("a cold start within a second of the file's epoch is the same") {
    const auto agrees =
        read_ok(dir / "a.nc", request(AdcircKind::elevation, everything(3),
                                      epoch + std::chrono::milliseconds{900}));
    CHECK(warning_count(agrees.warnings, WarningCode::cold_start_differs) == 0);
    const auto late =
        read_ok(dir / "a.nc", request(AdcircKind::elevation, everything(3),
                                      epoch + std::chrono::milliseconds{1100}));
    CHECK(warning_count(late.warnings, WarningCode::cold_start_differs) == 1);
  }
}

TEST_CASE("cold start: the unit of time:units is honoured when it is used",
          "[io][adcirc][netcdf]") {
  const mov::test::ScratchDir dir;
  AdcircNc spec = zeta_spec();
  spec.time_units = "minutes since 2010-01-01 00:00:00";
  spec.times = {10, 20, 30, 40, 50};
  make_adcirc_nc(dir / "a.nc", spec);
  const auto read = read_ok(dir / "a.nc", request(AdcircKind::elevation,
                                                  everything(3), std::nullopt));
  CHECK(read.value.times(StationIndex{0})[0] == at_seconds(600.0));
  CHECK(read.value.times(StationIndex{0})[4] == at_seconds(3000.0));
}

TEST_CASE("cold start: NCDATE placeholders need a cold start",
          "[io][adcirc][netcdf]") {
  const mov::test::ScratchDir dir;
  const auto required = [&](std::optional<std::string> units) {
    AdcircNc spec = zeta_spec();
    spec.time_units = std::move(units);
    make_adcirc_nc(dir / "a.nc", spec);
    return mov::io::read_adcirc_netcdf(
        dir / "a.nc",
        request(AdcircKind::elevation, everything(3), std::nullopt), {});
  };
  // What the legacy fixtures carry, a zero date, no date, and no attribute.
  for (const char* placeholder :
       {"seconds since Met", "seconds since 0000-00-00 00:00:00",
        "seconds since", "banana", ""}) {
    const auto result = required(std::string{placeholder});
    CHECK(format_error_of(result).code == FormatErrc::cold_start_required);
  }
  CHECK(format_error_of(required(std::nullopt)).code ==
        FormatErrc::cold_start_required);
  // The same file reads with a cold start.
  AdcircNc spec = zeta_spec();
  spec.time_units = "seconds since Met";
  make_adcirc_nc(dir / "b.nc", spec);
  CHECK(mov::io::read_adcirc_netcdf(
            dir / "b.nc", request(AdcircKind::elevation, everything(3)), {})
            .has_value());
}

TEST_CASE("cold start: a standard-calendar epoch before 1582 is refused",
          "[io][adcirc][netcdf]") {
  const mov::test::ScratchDir dir;
  AdcircNc spec = zeta_spec();
  spec.time_units = "seconds since 1500-01-01 00:00:00";
  make_adcirc_nc(dir / "a.nc", spec);
  const auto read = mov::io::read_adcirc_netcdf(
      dir / "a.nc", request(AdcircKind::elevation, everything(3), std::nullopt),
      {});
  CHECK(format_error_of(read).code == FormatErrc::unsupported_calendar);
}

TEST_CASE("cold start: the calendar of time is checked when the units are used",
          "[io][adcirc][netcdf]") {
  const mov::test::ScratchDir dir;
  AdcircNc spec = zeta_spec();
  spec.time_units = "seconds since 2011-02-03 04:05:06.00049";
  SECTION("a calendar that is not reproduced") {
    spec.time_calendar = "360_day";
    make_adcirc_nc(dir / "a.nc", spec);
    const auto read = mov::io::read_adcirc_netcdf(
        dir / "a.nc",
        request(AdcircKind::elevation, everything(3), std::nullopt), {});
    CHECK(format_error_of(read).code == FormatErrc::unsupported_calendar);
    CHECK(format_error_of(read).subject == "time:calendar");
    // A cold start makes the calendar of the file irrelevant.
    CHECK(mov::io::read_adcirc_netcdf(
              dir / "a.nc", request(AdcircKind::elevation, everything(3)), {})
              .has_value());
  }
  SECTION("a calendar that is, and digits beyond the millisecond") {
    spec.time_calendar = "proleptic_gregorian";
    make_adcirc_nc(dir / "a.nc", spec);
    const auto req =
        request(AdcircKind::elevation, everything(3), std::nullopt);
    const auto read = read_ok(dir / "a.nc", req);
    CHECK(warning_count(read.warnings, WarningCode::time_precision_dropped) ==
          1);
    CHECK(warning_count(read.warnings, WarningCode::epoch_used) == 1);
    const auto inspected =
        mov::io::inspect_adcirc_netcdf(dir / "a.nc", Epsg::wgs84(), {});
    REQUIRE(inspected.has_value());
    CHECK(warning_count(inspected->warnings,
                        WarningCode::time_precision_dropped) == 1);
  }
}

TEST_CASE("time: masked, out of range and non-increasing values are errors",
          "[io][adcirc][netcdf]") {
  const mov::test::ScratchDir dir;
  AdcircNc spec = zeta_spec();
  const auto failing = [&] {
    make_adcirc_nc(dir / "a.nc", spec);
    return mov::io::read_adcirc_netcdf(
        dir / "a.nc", request(AdcircKind::elevation, everything(3)), {});
  };
  SECTION("a repeated time") {
    spec.times = {600, 1200, 1200, 1800, 2400};
    const auto read = failing();
    CHECK(format_error_of(read).code == FormatErrc::time_not_increasing);
    CHECK(format_error_of(read).index == 2U);
  }
  SECTION("a time that goes back (a hot start)") {
    spec.times = {600, 1200, 1800, 900, 2400};
    const auto read = failing();
    CHECK(format_error_of(read).code == FormatErrc::time_not_increasing);
    CHECK(format_error_of(read).index == 3U);
  }
  SECTION("a time that is the fill value") {
    spec.times = {600, 1200, 1800, -1, 2400};
    spec.time_fill = -1;
    const auto read = failing();
    CHECK(format_error_of(read).code == FormatErrc::time_missing);
    CHECK(format_error_of(read).index == 3U);
  }
  SECTION("a time that is NaN") {
    spec.times = {600, not_a_number, 1800, 2400, 3000};
    const auto read = failing();
    CHECK(format_error_of(read).code == FormatErrc::time_missing);
    CHECK(format_error_of(read).index == 1U);
  }
  SECTION("a time beyond what a double of milliseconds holds") {
    spec.times = {600, 1200, 1800, 1e300, 2400};
    const auto read = failing();
    CHECK(format_error_of(read).code == FormatErrc::time_out_of_range);
    CHECK(format_error_of(read).index == 3U);
  }
  SECTION("a 64-bit integer time reads") {
    spec.time_type = TimeType::int64;
    make_adcirc_nc(dir / "a.nc", spec);
    const auto read =
        read_ok(dir / "a.nc", request(AdcircKind::elevation, everything(3)));
    CHECK(read.value.times(StationIndex{0})[4] == at_seconds(3000.0));
  }
}

// ---- the selection
// -----------------------------------------------------------------------------

TEST_CASE("the selection: order, empty, and made for another file",
          "[io][adcirc][netcdf]") {
  const mov::test::ScratchDir dir;
  make_adcirc_nc(dir / "a.nc", zeta_spec());
  SECTION("the table follows the order of the selection") {
    const auto read = read_ok(
        dir / "a.nc", request(AdcircKind::elevation, select({2, 0}, 3)));
    REQUIRE(read.value.size() == 2);
    CHECK(read.value.station(StationIndex{0}).id.view() == "2");
    CHECK(read.value.station(StationIndex{1}).id.view() == "0");
    CHECK(samples_of(read.value, 0, 0)[1] == sample(20.25));
    CHECK(samples_of(read.value, 1, 0)[1] == sample(0.25));
  }
  SECTION("an empty selection reads no station") {
    const auto read =
        read_ok(dir / "a.nc", request(AdcircKind::elevation, select({}, 3)));
    CHECK(read.value.size() == 0);
    CHECK(read.value.schema().size() == 1);
  }
  SECTION("a selection for another station count") {
    const auto read = mov::io::read_adcirc_netcdf(
        dir / "a.nc", request(AdcircKind::elevation, everything(4)), {});
    CHECK(format_error_of(read).code == FormatErrc::station_count_mismatch);
  }
}

// ---- reading in blocks
// -------------------------------------------------------------------------

TEST_CASE("blocks of any size, and any grouping, give the same table",
          "[io][adcirc][netcdf]") {
  const mov::test::ScratchDir dir;
  AdcircNc spec = velocity_spec();
  spec.stations = 9;
  spec.steps = 40;
  spec.value = [](std::size_t t, std::size_t s, std::size_t c) {
    if ((t * 7 + s * 3) % 11 == 0) {
      return -99999.0;  // fill here and there, in either component
    }
    return plain(t, s, c);
  };
  make_adcirc_nc(dir / "a.nc", spec);
  const auto req = request(AdcircKind::velocity, select({7, 0, 3, 4, 8, 1}, 9));

  const auto whole = read_ok(dir / "a.nc", req).value;
  for (const std::size_t slab : {1U, 7U, 9U, 100U, 360U}) {
    ReadContext ctx;
    ctx.limits.slab_elements = slab;
    CHECK(read_ok(dir / "a.nc", req, ctx).value == whole);
  }
  for (const std::size_t stride : {0U, 1U, 2U, 3U, 100U}) {
    for (const std::optional<std::size_t> chunk :
         {std::optional<std::size_t>{}, std::optional<std::size_t>{2},
          std::optional<std::size_t>{4}}) {
      const auto grouped = mov::io::detail::read_adcirc_netcdf(
          dir / "a.nc", req, {},
          mov::io::detail::GroupingPolicy{.stride = stride, .chunk = chunk});
      REQUIRE(grouped.has_value());
      CHECK(grouped->value == whole);
    }
  }
}

TEST_CASE("a time block read agrees with a read of each column",
          "[io][adcirc][netcdf]") {
  const mov::test::ScratchDir dir;
  AdcircNc spec = zeta_spec();
  spec.stations = 6;
  spec.steps = 25;
  spec.chunks = {5, 2};
  spec.fill = std::nullopt;  // no masking: the raw values are the samples
  spec.value = [](std::size_t t, std::size_t s, std::size_t) {
    return static_cast<double>(t * 100 + s);
  };
  make_adcirc_nc(dir / "a.nc", spec);
  ReadContext ctx;
  ctx.limits.slab_elements = 11;
  const auto read = read_ok(
      dir / "a.nc", request(AdcircKind::elevation, select({5, 2, 3}, 6)), ctx);

  // v4's way: one strided column at a time (a handle of its own, closed first).
  std::vector<std::vector<double>> columns;
  {
    auto file = mov::io::nc::File::open(dir / "a.nc", {});
    REQUIRE(file.has_value());
    for (const std::size_t s : {5U, 2U, 3U}) {
      auto column = file->read<double>(
          "zeta", {{.start = 0, .count = 25}, {.start = s, .count = 1}});
      REQUIRE(column.has_value());
      columns.push_back(*std::move(column));
    }
  }
  for (std::size_t p = 0; p < 3; ++p) {
    const auto got = samples_of(read.value, p, 0);
    REQUIRE(got.size() == 25);
    for (std::size_t t = 0; t < 25; ++t) {
      CHECK(got[t] == sample(columns[p][t]));
    }
  }
}

TEST_CASE("cancellation between blocks, and limits before the read",
          "[io][adcirc][netcdf]") {
  const mov::test::ScratchDir dir;
  AdcircNc spec = zeta_spec();
  spec.steps = 100;
  make_adcirc_nc(dir / "a.nc", spec);
  const auto req = request(AdcircKind::elevation, everything(3));

  SECTION("stopped from the start") {
    ReadContext ctx;
    ctx.stop = StopToken{[] { return true; }};
    const auto read = mov::io::read_adcirc_netcdf(dir / "a.nc", req, ctx);
    REQUIRE(not(read.has_value()));
    CHECK(std::holds_alternative<Cancelled>(read.error()));
  }
  SECTION("stopped after some blocks") {
    int polls = 0;
    ReadContext ctx;
    ctx.limits.slab_elements = 7;
    ctx.stop = StopToken{[&polls] { return ++polls > 20; }};
    const auto read = mov::io::read_adcirc_netcdf(dir / "a.nc", req, ctx);
    REQUIRE(not(read.has_value()));
    CHECK(std::holds_alternative<Cancelled>(read.error()));
    CHECK(polls > 20);
  }
  SECTION("more samples than max_elements") {
    ReadContext ctx;
    ctx.limits.max_elements = 299;  // 3 stations x 100 steps
    const auto read = mov::io::read_adcirc_netcdf(dir / "a.nc", req, ctx);
    REQUIRE(not(read.has_value()));
    const auto* error = nc_error_in(read.error());
    REQUIRE(error != nullptr);
    CHECK(error->status == mov::io::NcStatus{mov::io::WrapperFault::too_large});
    // One station fits.
    CHECK(mov::io::read_adcirc_netcdf(
              dir / "a.nc", request(AdcircKind::elevation, select({1}, 3)), ctx)
              .has_value());
  }
  SECTION("more stations than max_elements are not listed") {
    ReadContext ctx;
    ctx.limits.max_elements = 2;
    const auto inspected =
        mov::io::inspect_adcirc_netcdf(dir / "a.nc", Epsg::wgs84(), ctx);
    REQUIRE(not(inspected.has_value()));
    const auto* error = nc_error_in(inspected.error());
    REQUIRE(error != nullptr);
    CHECK(error->status == mov::io::NcStatus{mov::io::WrapperFault::too_large});
    CHECK(error->object == "station");
  }
  SECTION("a span wider than max_elements is read in windows of time") {
    // Stations 0 and 2 span three stations: 300 elements, over the limit; they
    // are 200 samples and what is held at once is one block.
    ReadContext ctx;
    ctx.limits.max_elements = 250;
    ctx.limits.slab_elements = 7;
    const auto windowed = mov::io::read_adcirc_netcdf(
        dir / "a.nc", request(AdcircKind::elevation, select({0, 2}, 3)), ctx);
    REQUIRE(windowed.has_value());
    const auto whole = read_ok(
        dir / "a.nc", request(AdcircKind::elevation, select({0, 2}, 3)));
    CHECK(windowed->value == whole.value);
  }
  SECTION("more bytes than max_result_bytes") {
    ReadContext ctx;
    ctx.limits.max_result_bytes = 300 * 16 - 1;
    const auto read = mov::io::read_adcirc_netcdf(dir / "a.nc", req, ctx);
    REQUIRE(not(read.has_value()));
    const auto* error = nc_error_in(read.error());
    REQUIRE(error != nullptr);
    CHECK(error->status == mov::io::NcStatus{mov::io::WrapperFault::too_large});
  }
}

// ---- positions
// -----------------------------------------------------------------------------------

TEST_CASE("positions in another CRS are projected, the native point is kept",
          "[io][adcirc][netcdf][projection]") {
  mov::test::configure_projection_database();
  const mov::test::ScratchDir dir;
  AdcircNc spec = zeta_spec();
  spec.x = {500000.0, 600000.0, 400000.0};
  spec.y = {3000000.0, 3100000.0, 2900000.0};
  make_adcirc_nc(dir / "a.nc", spec);
  const auto crs = mov::test::epsg(26915);  // NAD83 / UTM zone 15N
  const auto read =
      read_ok(dir / "a.nc",
              request(AdcircKind::elevation, everything(3), cold_start(), crs));
  const auto& station = read.value.station(StationIndex{0});
  CHECK(station.location.lon() == Catch::Approx(-93.0).margin(1e-4));
  CHECK(station.location.lat() > 27.0);
  CHECK(station.location.lat() < 27.2);
  const auto native = station.native;
  REQUIRE(native.has_value());
  if (native) {
    CHECK(native->x() == 500000.0);
    CHECK(native->y() == 3000000.0);
    CHECK(native->crs() == crs);
  }
  // NAD83 to WGS 84 without grids is a datum shift of a few metres, which
  // PROJ says (WP5); the count is the number of points converted.
  CHECK(warning_count(read.warnings, WarningCode::crs_approximate) == 3);
}

TEST_CASE("positions that cannot be converted are errors with the station",
          "[io][adcirc][netcdf][projection]") {
  mov::test::configure_projection_database();
  const mov::test::ScratchDir dir;
  AdcircNc spec = zeta_spec();
  spec.y = {29.0, 95.0, 27.0};  // station 1 is north of the pole
  spec.x = {-90.0, -91.0, -92.0};
  make_adcirc_nc(dir / "a.nc", spec);
  const auto read = mov::io::read_adcirc_netcdf(
      dir / "a.nc", request(AdcircKind::elevation, everything(3)), {});
  CHECK(format_error_of(read).code == FormatErrc::bad_coordinates);
  CHECK(format_error_of(read).station == 1U);

  // A point PROJ cannot transform is as bad.
  spec.x = {500000.0, 1e15, 500000.0};
  spec.y = {3000000.0, 3000000.0, 3000000.0};
  make_adcirc_nc(dir / "c.nc", spec);
  const auto far =
      mov::io::read_adcirc_netcdf(dir / "c.nc",
                                  request(AdcircKind::elevation, everything(3),
                                          cold_start(), mov::test::epsg(32615)),
                                  {});
  CHECK(format_error_of(far).code == FormatErrc::bad_coordinates);
  CHECK(format_error_of(far).station == 1U);

  make_adcirc_nc(dir / "b.nc", zeta_spec());
  const auto unknown = mov::io::read_adcirc_netcdf(
      dir / "b.nc",
      request(AdcircKind::elevation, everything(3), cold_start(),
              mov::test::epsg(999999)),
      {});
  CHECK(format_error_of(unknown).code == FormatErrc::unsupported_crs);
}

// ---- inspect
// ----------------------------------------------------------------------------------------

TEST_CASE("inspect: kind, variables, stations, time units",
          "[io][adcirc][netcdf]") {
  const mov::test::ScratchDir dir;
  AdcircNc spec = velocity_spec();
  spec.station_names = {"One", "Two", "Three"};
  spec.time_units = "seconds since 2012-06-01 00:00:00";
  make_adcirc_nc(dir / "a.nc", spec);
  const auto inspected =
      mov::io::inspect_adcirc_netcdf(dir / "a.nc", Epsg::wgs84(), {});
  REQUIRE(inspected.has_value());
  const auto& catalog = inspected->value;
  CHECK(catalog.kind == AdcircKind::velocity);
  CHECK(mov::io::adcirc_variables(catalog.kind) ==
        std::vector<std::string>{"u-vel", "v-vel"});
  CHECK(catalog.times == 5);
  REQUIRE(catalog.stations.size() == 3);
  CHECK(catalog.stations[2].name.view() == "Three");
  CHECK(catalog.stations[2].id.view() == "2");
  REQUIRE(catalog.time_units.has_value());
  if (catalog.time_units) {
    CHECK(catalog.time_units->text == "seconds since 2012-06-01 00:00:00");
    REQUIRE(catalog.time_units->parsed.has_value());
    if (catalog.time_units->parsed) {
      CHECK(catalog.time_units->parsed->unit == mov::io::CfTimeUnit::second);
    }
  }

  // A placeholder is reported as text and not parsed.
  spec.time_units = "seconds since Met";
  make_adcirc_nc(dir / "b.nc", spec);
  const auto placeholder =
      mov::io::inspect_adcirc_netcdf(dir / "b.nc", Epsg::wgs84(), {});
  REQUIRE(placeholder.has_value());
  CHECK(placeholder->value.time_units ==
        std::optional<mov::io::TimeUnitsAttr>{
            {.text = "seconds since Met", .parsed = std::nullopt}});
}

TEST_CASE("the readers close their file: it can be opened again at once",
          "[io][adcirc][netcdf]") {
  // One handle per file: in a debug build a reader that leaked its handle
  // would make the next open assert.
  const mov::test::ScratchDir dir;
  make_adcirc_nc(dir / "a.nc", zeta_spec());
  const auto path = dir / "a.nc";
  for (int round = 0; round < 3; ++round) {
    CHECK(mov::io::inspect_adcirc_netcdf(path, Epsg::wgs84(), {}).has_value());
    CHECK(mov::io::read_adcirc_netcdf(
              path, request(AdcircKind::elevation, everything(3)), {})
              .has_value());
    CHECK(mov::io::read_adcirc_netcdf(
              path, request(AdcircKind::pressure, everything(3)), {})
              .has_value() == false);  // fails, and closes all the same
    CHECK(mov::io::nc::File::open(path, {}).has_value());
  }
}

// ---- the legacy fixtures
// ----------------------------------------------------------
//
// MetOceanViewer/function_tests/ReadADCIRC/netCDF/fort.6x.nc are 4 MB files
// for three stations (the data is a few kB; the rest is HDF5 chunking and
// attributes). They are read in place and the tests skip when the legacy tree
// is not there. The same run's ASCII output is
// tests/fixtures/io/adcirc/legacy/fort.6x.

namespace {

struct LegacyOutput {
  const char* file;
  AdcircKind kind;
  std::size_t times;
};

constexpr std::array<LegacyOutput, 4> legacy_outputs{{
    {.file = "fort.61", .kind = AdcircKind::elevation, .times = 144},
    {.file = "fort.62", .kind = AdcircKind::velocity, .times = 48},
    {.file = "fort.71", .kind = AdcircKind::pressure, .times = 48},
    {.file = "fort.72", .kind = AdcircKind::wind, .times = 48},
}};

std::filesystem::path legacy_nc(const LegacyOutput& output) {
  const auto path = mov::test::legacy_file(std::string{"ReadADCIRC/netCDF/"} +
                                           output.file + ".nc");
  if (not path) {
    SKIP("the legacy tree (MetOceanViewer/function_tests) is not here");
  }
  return *path;
}

}  // namespace

TEST_CASE("legacy netCDF: the catalog of each output",
          "[io][adcirc][netcdf][legacy]") {
  for (const LegacyOutput& output : legacy_outputs) {
    CAPTURE(output.file);
    const auto path = legacy_nc(output);
    const auto inspected =
        mov::io::inspect_adcirc_netcdf(path, Epsg::wgs84(), {});
    REQUIRE(inspected.has_value());
    const auto& catalog = inspected->value;
    CHECK(catalog.kind == output.kind);
    CHECK(catalog.times == output.times);
    // station_name is there ("Station One"...), where v4 numbered the stations.
    REQUIRE(catalog.stations.size() == 3);
    CHECK(catalog.stations[0].name.view() == "Station One");
    CHECK(catalog.stations[1].name.view() == "Station Two");
    CHECK(catalog.stations[2].name.view() == "Station Three");
    // x and y are floats in these files: the double is the float's value.
    CHECK(catalog.stations[0].location.lon() == static_cast<double>(-90.0127F));
    CHECK(catalog.stations[0].location.lat() ==
          static_cast<double>(29.987793F));
    CHECK(catalog.stations[1].location.lon() == -90.5);
    CHECK(catalog.stations[2].location.lat() == 25.0);
    // The units attribute is the NCDATE placeholder of these runs.
    CHECK(catalog.time_units ==
          std::optional<mov::io::TimeUnitsAttr>{
              {.text = "seconds since Met", .parsed = std::nullopt}});
  }
}

TEST_CASE("legacy netCDF: a cold start is needed",
          "[io][adcirc][netcdf][legacy]") {
  const auto path = legacy_nc(legacy_outputs[0]);
  const auto result = mov::io::read_adcirc_netcdf(
      path, request(AdcircKind::elevation, everything(3), std::nullopt), {});
  CHECK(format_error_of(result).code == FormatErrc::cold_start_required);
}

TEST_CASE("legacy netCDF: first and last values are pinned",
          "[io][adcirc][netcdf][legacy]") {
  const auto read = [](const LegacyOutput& output) {
    return read_ok(legacy_nc(output), request(output.kind, everything(3)));
  };
  SECTION("fort.61: the first station is dry throughout") {
    const auto r = read(legacy_outputs[0]);
    CHECK(samples_of(r.value, 0, 0) == std::vector<Sample>(144, Sample{Dry{}}));
    const auto st1 = samples_of(r.value, 1, 0);
    CHECK(st1.front() == sample(0.91211551722566142));
    CHECK(st1.back() == sample(1.2906693860101193));
    CHECK(r.value.times(StationIndex{0}).front() == at_seconds(600.0));
    CHECK(r.value.times(StationIndex{0}).back() == at_seconds(86400.0));
    CHECK(r.warnings.empty());
  }
  SECTION("fort.62: u and v, and the magnitude (parity with the ASCII B1)") {
    const auto r = read(legacy_outputs[1]);
    CHECK(samples_of(r.value, 0, 0).front() == sample(0.049298094832642407));
    CHECK(samples_of(r.value, 0, 0).back() == sample(0.06176053710657186));
    CHECK(samples_of(r.value, 0, 1).front() == sample(0.012227184138624888));
    CHECK(samples_of(r.value, 0, 1).back() == sample(-0.024420385957614345));
    const auto vector = mov::core::vector_series(
        r.value, StationIndex{0}, ColumnIndex{0}, ColumnIndex{1});
    REQUIRE(vector.has_value());
    const auto speed = vector->magnitude();
    CHECK(number(speed.samples().front()) ==
          Catch::Approx(0.0507918).epsilon(1e-6));
  }
  SECTION("fort.71: pressure") {
    const auto r = read(legacy_outputs[2]);
    CHECK(samples_of(r.value, 0, 0).front() == sample(10.322769043306057));
    CHECK(samples_of(r.value, 0, 0).back() == sample(10.316598498432811));
  }
  SECTION("fort.72: wind") {
    const auto r = read(legacy_outputs[3]);
    CHECK(samples_of(r.value, 0, 0).front() == sample(-8.4382305551879391));
    CHECK(samples_of(r.value, 0, 0).back() == sample(-8.9259051831365728));
    CHECK(samples_of(r.value, 0, 1).front() == sample(-2.1457132325492489));
    CHECK(samples_of(r.value, 0, 1).back() == sample(-3.1629436371490445));
  }
}

TEST_CASE("legacy: the ASCII and the netCDF output of one run agree to 1e-9",
          "[io][adcirc][netcdf][legacy]") {
  for (const LegacyOutput& output : legacy_outputs) {
    CAPTURE(output.file);
    const auto nc =
        read_ok(legacy_nc(output), request(output.kind, everything(3)));
    const auto ascii = mov::io::read_adcirc_ascii(
        mov::test::fixture(std::string{"io/adcirc/legacy/"} + output.file),
        mov::test::fixture("io/adcirc/legacy/stations.csv"), Epsg::wgs84(),
        {.kind = output.kind,
         .cold_start = cold_start(),
         .stations = everything(3)},
        {});
    REQUIRE(ascii.has_value());
    const StationTable& a = ascii->value;
    const StationTable& n = nc.value;
    // The same columns on the same axis; only the names differ.
    REQUIRE(a.schema().size() == n.schema().size());
    for (std::size_t k = 0; k < a.schema().size(); ++k) {
      CHECK(a.schema()[k] == n.schema()[k]);
    }
    for (std::size_t s = 0; s < 3; ++s) {
      const StationIndex station{s};
      const auto a_times = a.times(station);
      const auto n_times = n.times(station);
      REQUIRE(std::vector(a_times.begin(), a_times.end()) ==
              std::vector(n_times.begin(), n_times.end()));
      for (std::size_t k = 0; k < a.schema().size(); ++k) {
        const auto from_ascii = samples_of(a, s, k);
        const auto from_nc = samples_of(n, s, k);
        REQUIRE(from_ascii.size() == from_nc.size());
        for (std::size_t t = 0; t < from_nc.size(); ++t) {
          const auto x = from_ascii[t].value();
          const auto y = from_nc[t].value();
          REQUIRE(x.has_value() == y.has_value());
          if (x and y) {
            CHECK(std::abs(*x - *y) <= 1e-9 * std::max(1.0, std::abs(*y)));
          } else {
            CHECK(from_ascii[t] == from_nc[t]);  // both Dry or both Missing
          }
        }
      }
    }
  }
}

// ---- design decision 28: the grid decides what the vector components are
// --------

namespace {

// Station positions that are valid in EPSG:26915 (and nowhere as degrees).
AdcircNc projected_spec(AdcircNc spec) {
  spec.x = {500000.0, 510000.0, 520000.0};
  spec.y = {3000000.0, 3010000.0, 3020000.0};
  return spec;
}

}  // namespace

TEST_CASE("a geographic CRS keeps the registry's eastward and northward",
          "[io][adcirc][netcdf][regression][D28]") {
  mov::test::configure_projection_database();
  const mov::test::ScratchDir dir;
  make_adcirc_nc(dir / "a.nc", velocity_spec());
  // EPSG:4269 (NAD83) is geographic though it is not EPSG:4326.
  for (const int code : {4326, 4269}) {
    const auto read =
        read_ok(dir / "a.nc", request(AdcircKind::velocity, everything(3),
                                      cold_start(), mov::test::epsg(code)));
    CHECK(mov::core::token(read.value.schema()[0].quantity()) == "current_u");
    CHECK(mov::core::token(read.value.schema()[1].quantity()) == "current_v");
  }
}

TEST_CASE("a projected CRS makes the components grid-relative",
          "[io][adcirc][netcdf][regression][D28]") {
  mov::test::configure_projection_database();
  const mov::test::ScratchDir dir;
  const auto crs = mov::test::epsg(26915);
  SECTION("velocity: generic CF grid names, paired by assume_components") {
    AdcircNc spec = projected_spec(velocity_spec());
    spec.value = [](std::size_t, std::size_t, std::size_t c) {
      return c == 0 ? 3.0 : 4.0;
    };
    make_adcirc_nc(dir / "a.nc", spec);
    const auto read = read_ok(
        dir / "a.nc",
        request(AdcircKind::velocity, everything(3), cold_start(), crs));
    const auto& schema = read.value.schema();
    REQUIRE(schema.size() == 2);
    CHECK(mov::core::token(schema[0].quantity()) == "sea_water_x_velocity");
    CHECK(mov::core::token(schema[1].quantity()) == "sea_water_y_velocity");
    CHECK(schema[0].label() == "grid-relative current x");
    // The registry's pair would refuse them; the declared pair works.
    CHECK(not(mov::core::vector_series(read.value, StationIndex{0},
                                       ColumnIndex{0}, ColumnIndex{1})
                  .has_value()));
    const auto vector = mov::core::VectorSeries::assume_components(
        read.value.series(StationIndex{0}, ColumnIndex{0}),
        read.value.series(StationIndex{0}, ColumnIndex{1}));
    REQUIRE(vector.has_value());
    if (vector) {
      const auto speed = vector->magnitude();
      CHECK(number(speed.samples()[0]) == 5.0);
    }
  }
  SECTION("wind: x_wind and y_wind") {
    AdcircNc spec = projected_spec(AdcircNc{});
    spec.variables = {"windx", "windy"};
    make_adcirc_nc(dir / "a.nc", spec);
    const auto read =
        read_ok(dir / "a.nc",
                request(AdcircKind::wind, everything(3), cold_start(), crs));
    CHECK(mov::core::token(read.value.schema()[0].quantity()) == "x_wind");
    CHECK(mov::core::token(read.value.schema()[1].quantity()) == "y_wind");
  }
  SECTION("scalars are not vectors") {
    make_adcirc_nc(dir / "a.nc", projected_spec(zeta_spec()));
    const auto read = read_ok(
        dir / "a.nc",
        request(AdcircKind::elevation, everything(3), cold_start(), crs));
    CHECK(mov::core::token(read.value.schema()[0].quantity()) == "water_level");
  }
}

TEST_CASE("the ASCII reader takes the grid from the station file's CRS",
          "[io][adcirc][ascii][regression][D28]") {
  mov::test::configure_projection_database();
  const std::string stations_text =
      "3\n500000 3000000\n500010 3000000\n500020 3000000\n";
  const auto text = mov::test::fixture_text("io/adcirc/legacy/fort.62");
  for (const bool projected : {false, true}) {
    CAPTURE(projected);
    // The same three stations, as degrees or as UTM metres.
    const auto stations = mov::io::parse_adcirc_station_file(
        projected ? stations_text : "3\n-90 30\n-91 29\n-92 28\n",
        projected ? mov::test::epsg(26915) : Epsg::wgs84(), ReadContext{});
    REQUIRE(stations.has_value());
    const auto parsed =
        mov::io::parse_adcirc_ascii(text, stations->value,
                                    {.kind = AdcircKind::velocity,
                                     .cold_start = cold_start(),
                                     .stations = everything(3)},
                                    ReadContext{});
    REQUIRE(parsed.has_value());
    CHECK(mov::core::token(parsed->value.schema()[0].quantity()) ==
          (projected ? "sea_water_x_velocity" : "current_u"));
  }
}

TEST_CASE("the schema has one column per data variable",
          "[io][adcirc][netcdf]") {
  for (const AdcircKind kind : {AdcircKind::elevation, AdcircKind::velocity,
                                AdcircKind::pressure, AdcircKind::wind}) {
    for (const mov::io::CrsKind grid :
         {mov::io::CrsKind::geographic, mov::io::CrsKind::projected}) {
      CHECK(mov::io::adcirc_variables(kind).size() ==
            mov::io::detail::adcirc_schema(kind, grid).size());
    }
  }
  // Only the vectors have a pair, and the pair is the first and second.
  CHECK(mov::io::adcirc_variables(AdcircKind::wind) ==
        std::vector<std::string>{"windx", "windy"});
}

TEST_CASE("the global ics is checked against the CRS", "[io][adcirc][netcdf]") {
  mov::test::configure_projection_database();
  const mov::test::ScratchDir dir;
  struct Case {
    std::optional<int> ics;
    bool projected;
    std::size_t warnings;
  };
  for (const Case c :
       {Case{
            .ics = 2, .projected = false, .warnings = 0},  // spherical, degrees
        Case{.ics = 1, .projected = true, .warnings = 0},  // Cartesian, UTM
        Case{.ics = 2, .projected = true, .warnings = 1},  // says degrees
        Case{.ics = 1, .projected = false, .warnings = 1},  // says metres
        Case{.ics = std::nullopt, .projected = true, .warnings = 0},
        Case{.ics = 20, .projected = true, .warnings = 0}}) {  // not 1 or 2
    CAPTURE(c.ics.value_or(0), c.projected);
    AdcircNc spec = c.projected ? projected_spec(zeta_spec()) : zeta_spec();
    spec.ics = c.ics;
    make_adcirc_nc(dir / "a.nc", spec);
    const Epsg crs = c.projected ? mov::test::epsg(26915) : Epsg::wgs84();
    const auto read = read_ok(
        dir / "a.nc",
        request(AdcircKind::elevation, everything(3), cold_start(), crs));
    CHECK(warning_count(read.warnings, WarningCode::crs_mismatch) ==
          c.warnings);
    const auto inspected =
        mov::io::inspect_adcirc_netcdf(dir / "a.nc", crs, ReadContext{});
    REQUIRE(inspected.has_value());
    CHECK(warning_count(inspected->warnings, WarningCode::crs_mismatch) ==
          c.warnings);
  }
}

// ---- review fixes
// ---------------------------------------------------------------------

TEST_CASE("a 64-bit time is masked by its attributes like any other",
          "[io][adcirc][netcdf]") {
  const mov::test::ScratchDir dir;
  AdcircNc spec = zeta_spec();
  spec.time_type = TimeType::int64;
  spec.time_fill = -1;
  spec.times = {-1, 1200, 1800, 2400, 3000};  // the fill is the first time
  make_adcirc_nc(dir / "a.nc", spec);
  const auto read = mov::io::read_adcirc_netcdf(
      dir / "a.nc", request(AdcircKind::elevation, everything(3)), {});
  CHECK(format_error_of(read).code == FormatErrc::time_missing);
  CHECK(format_error_of(read).index == 0U);
}

TEST_CASE("a model attribute that is not text is not ADCIRC's",
          "[io][adcirc][netcdf]") {
  const mov::test::ScratchDir dir;
  AdcircNc spec = zeta_spec();
  spec.model_as_number = true;
  make_adcirc_nc(dir / "a.nc", spec);
  const auto inspected =
      mov::io::inspect_adcirc_netcdf(dir / "a.nc", Epsg::wgs84(), {});
  CHECK(format_error_of(inspected).code == FormatErrc::not_this_format);
}

TEST_CASE("legacy netCDF: the global ics is spherical, as EPSG:4326 is",
          "[io][adcirc][netcdf][legacy]") {
  const auto path = legacy_nc(legacy_outputs[0]);
  auto file = mov::io::nc::File::open(path, {});
  REQUIRE(file.has_value());
  if (file) {
    const auto ics =
        file->numeric_att<std::int32_t>(mov::io::nc::global, "ics");
    INFO((ics ? std::string{} : mov::test::what(mov::io::Error{ics.error()})));
    REQUIRE(ics.has_value());
    CHECK(ics.value_or(std::nullopt) ==
          std::optional<std::vector<std::int32_t>>{{2}});
  }
}
