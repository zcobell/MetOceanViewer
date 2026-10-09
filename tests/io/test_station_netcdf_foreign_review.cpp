// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The WP10b review round (owner decision 30): how a foreign CF file's time
// variable is chosen, how its samples are bounded, the name variables, the
// token rules shared with the writer, quality flags, datums, observed and
// predicted water levels, the padding options, the order of the warnings. The
// reviewer's probes are the "probe" cases.

#include <netcdf.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "foreign_support.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/quantity.hpp"
#include "mov/io/error.hpp"
#include "mov/io/station_netcdf.hpp"
#include "mov/io/warning.hpp"
#include "nc_edit.hpp"

namespace {

using namespace mov::test::foreign;  // NOLINT(google-build-using-namespace)
namespace core = mov::core;
namespace io = mov::io;
using gen::Cdf;
using gen::CfKind;
using gen::CfSpec;
using io::FormatErrc;
using io::WarningCode;
using mov::test::ncgen::Editor;

// ---- a small file of our own
// -------------------------------------------------

/// Hooks into the file `write_mini` builds (two stations of 8-byte ids, two
/// times, `temp` over (station, time)).
struct MiniHooks {
  std::string id_name{"station_id"};
  std::string time_name{"time"};
  /// Before the time variable: variables a reader might mistake for it.
  std::function<void(Cdf&, int station, int time)> before_time;
  /// After `temp`, before any data is written.
  std::function<void(Cdf&, int station, int time, int width)> extra;
};

void write_mini(const std::filesystem::path& path, const MiniHooks& hooks) {
  Cdf f{path};
  f.text("", "Conventions", "CF-1.8");
  f.text("", "featureType", "timeSeries");
  const int station = f.dim("station", 2);
  const int time = f.dim("time", 2);
  const int width = f.dim("strlen", 8);
  f.var(hooks.id_name, NC_CHAR, {station, width});
  f.text(hooks.id_name, "cf_role", "timeseries_id");
  f.var("lat", NC_DOUBLE, {station});
  f.text("lat", "units", "degrees_north");
  f.var("lon", NC_DOUBLE, {station});
  f.text("lon", "units", "degrees_east");
  if (hooks.before_time) {
    hooks.before_time(f, station, time);
  }
  f.var(hooks.time_name, NC_DOUBLE, {time});
  f.text(hooks.time_name, "units", "hours since 2000-01-01 00:00:00");
  f.var("temp", NC_DOUBLE, {station, time});
  f.text("temp", "units", "degC");
  if (hooks.extra) {
    hooks.extra(f, station, time, width);
  }
  f.put_rows(hooks.id_name, 8, {"AAA", "BBB"});
  f.put("lat", std::vector<double>{29.0, 30.0});
  f.put("lon", std::vector<double>{-90.0, -91.0});
  f.put(hooks.time_name, std::vector<double>{0.0, 1.0});
  f.put("temp", std::vector<double>{1.0, 2.0, 3.0, 4.0});
  f.close();
}

struct MiniFile {
  explicit MiniFile(const MiniHooks& hooks) : path{dir / "mini.nc"} {
    write_mini(path, hooks);
  }
  [[nodiscard]] io::Read<io::StationFile> read() const {
    return must_read(read_all(path));
  }
  [[nodiscard]] io::FormatError error() const {
    return format_error_of(read_all(path));
  }
  mov::test::ScratchDir dir;
  std::filesystem::path path;
};

// ---- the time variable (B1)
// ----------------------------------------------------

}  // namespace

TEST_CASE("probe: a time-like variable over the time dimension is not the time",
          "[io][station_nc][foreign][review]") {
  MiniHooks hooks;
  // The reviewer's probe 2: a run time defined first, with time units and the
  // same dimension.
  hooks.before_time = [](Cdf& f, int /*station*/, int time) {
    f.var("time_run", NC_DOUBLE, {time});
    f.text("time_run", "units", "hours since 2020-01-01");
    f.text("time_run", "long_name", "run time");
    f.put("time_run", std::vector<double>{-100.0, -99.0});
  };
  const MiniFile file{hooks};
  const auto read = file.read();
  const auto times = read.value.table.times(core::StationIndex{0});
  REQUIRE(times.size() == 2);
  CHECK(times[0] == hours(0));
  CHECK(times[1] == hours(1));
}

TEST_CASE("a vector of times over the stations is an instance variable",
          "[io][station_nc][foreign][review]") {
  MiniHooks hooks;
  hooks.before_time = [](Cdf& f, int station, int /*time*/) {
    f.var("deployed", NC_DOUBLE, {station});
    f.text("deployed", "units", "days since 1990-01-01");
    f.put("deployed", std::vector<double>{100.0, 200.0});
  };
  const MiniFile file{hooks};
  const auto read = file.read();
  CHECK(read.value.table.times(core::StationIndex{1})[1] == hours(1));
  // It is not a series either.
  CHECK(count_of(read.warnings, WarningCode::skipped_variable) == 0);
}

TEST_CASE("the time variable is the best ranked, and a tie is refused",
          "[io][station_nc][foreign][review]") {
  const auto two_times = [](bool first_is_labelled, bool second_is_listed) {
    MiniHooks hooks;
    hooks.time_name = "t_a";
    hooks.extra = [=](Cdf& f, int /*station*/, int time, int /*width*/) {
      f.var("t_b", NC_DOUBLE, {time});
      f.text("t_b", "units", "hours since 2000-01-01 00:00:00");
      f.put("t_b", std::vector<double>{10.0, 11.0});
      if (first_is_labelled) {
        f.text("t_a", "standard_name", "time");
      }
      if (second_is_listed) {
        f.text("temp", "coordinates", "t_b lat lon");
      }
    };
    return hooks;
  };
  SECTION("two with only units: unsupported_layout naming both") {
    const MiniFile file{two_times(false, false)};
    const auto e = file.error();
    CHECK(e.code == FormatErrc::unsupported_layout);
    CHECK(e.subject == "t_a, t_b");
    CHECK(format_error_of(io::inspect_station_netcdf(file.path, {})).code ==
          FormatErrc::unsupported_layout);
  }
  SECTION("the standard name beats units alone") {
    const MiniFile file{two_times(true, false)};
    const auto read = file.read();
    CHECK(read.value.table.times(core::StationIndex{0})[0] == hours(0));
  }
  SECTION("being listed in `coordinates` beats a standard name") {
    const MiniFile file{two_times(true, true)};
    const auto read = file.read();
    CHECK(read.value.table.times(core::StationIndex{0})[0] == hours(10));
  }
}

TEST_CASE("two labelled time variables of an incomplete layout are a tie",
          "[io][station_nc][foreign][review]") {
  CfSpec spec = ragged(CfKind::incomplete);
  spec.customize = [](Cdf& f, int station, int sample) {
    f.var("time_b", NC_DOUBLE, {station, sample});
    f.text("time_b", "units", "hours since 2000-01-01 00:00:00");
    f.text("time_b", "standard_name", "time");
  };
  const auto e = error_of_spec(spec);
  CHECK(e.code == FormatErrc::unsupported_layout);
  CHECK(e.subject == "time, time_b");
}

// ---- bounded reads (B2)
// ----------------------------------------------------------

TEST_CASE("probe: a time-major incomplete layout is read as far as its samples",
          "[io][station_nc][foreign][review]") {
  // The reviewer's probe 1: obs = 2^40 with obs_count of 2 and 1. The matrix
  // is (obs, station); a read of the whole dimension is a terabyte.
  const mov::test::ScratchDir dir;
  const auto path = dir / "huge.nc";
  {
    Cdf f{path};
    f.text("", "Conventions", "CF-1.8");
    f.text("", "featureType", "timeSeries");
    const int station = f.dim("station", 2);
    const int obs = f.dim("obs", std::size_t{1} << 40U);
    const int width = f.dim("strlen", 4);
    f.var("station_name", NC_CHAR, {station, width});
    f.text("station_name", "cf_role", "timeseries_id");
    f.var("lat", NC_DOUBLE, {station});
    f.text("lat", "units", "degrees_north");
    f.var("lon", NC_DOUBLE, {station});
    f.text("lon", "units", "degrees_east");
    f.chunked("time", NC_DOUBLE, {obs, station}, {4096, 2});
    f.text("time", "units", "hours since 2000-01-01 00:00:00");
    f.num("time", "_FillValue", NC_DOUBLE, {-1.0});
    f.chunked("temp", NC_DOUBLE, {obs, station}, {4096, 2});
    f.text("temp", "units", "degC");
    f.num("temp", "_FillValue", NC_DOUBLE, {-999.0});
    f.var("obs_count", NC_INT, {station});
    f.put_rows("station_name", 4, {"AAAA", "BBBB"});
    f.put("lat", std::vector<double>{29.0, 30.0});
    f.put("lon", std::vector<double>{-90.0, -91.0});
    f.put("obs_count", std::vector<double>{2.0, 1.0});
    // Rows 0 and 1 of both stations; station B has no second sample.
    f.put_block("time", 0, 0, 2, 2, std::vector<double>{0.0, 0.0, 1.0, -1.0});
    f.put_block("temp", 0, 0, 2, 2,
                std::vector<double>{10.0, 20.0, 11.0, -999.0});
    f.close();
  }
  const auto started = std::chrono::steady_clock::now();
  const auto read = must_read(read_all(path));
  const core::StationTable& t = read.value.table;
  REQUIRE(t.size() == 2);
  REQUIRE(t.times(core::StationIndex{0}).size() == 2);
  REQUIRE(t.times(core::StationIndex{1}).size() == 1);
  CHECK(t.times(core::StationIndex{0})[1] == hours(1));
  CHECK(at(t, 0, 0, 1) == v(11.0));
  CHECK(at(t, 1, 0, 0) == v(20.0));
  CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(30));

  // Checking every element of the padding is a terabyte too, and is refused.
  const auto whole = read_all(path, {}, {.padding = io::PaddingCheck::whole});
  REQUIRE_FALSE(whole.has_value());
  const auto* nc = std::get_if<io::NcError>(&whole.error());
  REQUIRE(nc != nullptr);
  CHECK(nc->status == io::NcStatus{io::WrapperFault::too_large});
}

// ---- padding options (S4)
// -------------------------------------------------------

namespace {

/// An incomplete layout of three stations with 2, 2 and 6 samples (obs = 6),
/// with `obs_count`.
CfSpec padded(bool transposed) {
  CfSpec spec;
  spec.kind = CfKind::incomplete;
  spec.transposed = transposed;
  spec.times = {{0, 1}, {0, 1}, {0, 1, 2, 3, 4, 5}};
  spec.values = {{20, 21}, {30, 31}, {40, 41, 42, 43, 44, 45}};
  spec.obs_count = std::vector<std::int64_t>{2, 2, 6};
  return spec;
}

/// Puts a value at the sample `j` of `station` of `temperature`.
void put_temperature(const std::filesystem::path& path, bool transposed,
                     std::size_t station, std::size_t j, double value) {
  Editor edit{path};
  if (transposed) {
    edit.put("temperature", {j, station}, value);
  } else {
    edit.put("temperature", {station, j}, value);
  }
}

}  // namespace

TEST_CASE("foreign incomplete: the padding options of SN 12.4 are honoured",
          "[io][station_nc][foreign][review]") {
  for (const bool transposed : {false, true}) {
    INFO((transposed ? "(time, station)" : "(station, time)"));
    const FixtureFile file{padded(transposed)};
    const auto only_a = core::StationSelection::make({0}, 3);
    REQUIRE(only_a.has_value());
    const auto read_a = [&](io::PaddingCheck padding) {
      return io::read_station_netcdf(file.path(),
                                     io::StationNcSelection{*only_a}, {},
                                     {.padding = padding});
    };
    // Without anything in the padding both pass.
    CHECK(read_a(io::PaddingCheck::boundary).has_value());
    CHECK(read_a(io::PaddingCheck::whole).has_value());

    // A value far in the padding of station A: the boundary check, which
    // looks at the first element after the samples, does not see it.
    put_temperature(file.path(), transposed, 0, 5, 7.0);
    CHECK(read_a(io::PaddingCheck::boundary).has_value());
    const auto whole = read_a(io::PaddingCheck::whole);
    const io::FormatError e = format_error_of(whole);
    CHECK(e.code == FormatErrc::padding_not_missing);
    CHECK(e.station == 0);
    CHECK(e.index == 5);

    // A value at the first padding element is seen by both.
    put_temperature(file.path(), transposed, 0, 2, 7.0);
    const io::FormatError at_boundary =
        format_error_of(read_a(io::PaddingCheck::boundary));
    CHECK(at_boundary.code == FormatErrc::padding_not_missing);
    CHECK(at_boundary.index == 2);
    // The default is the boundary check.
    CHECK(format_error_of(read_all(file.path())).code ==
          FormatErrc::padding_not_missing);
  }
}

TEST_CASE("foreign incomplete without obs_count counts the leading times",
          "[io][station_nc][foreign][review]") {
  // There is no padding to look at: a time after a missing one is the error.
  CfSpec spec = padded(false);
  spec.obs_count.reset();
  const FixtureFile file{spec};
  {
    const auto read = file.read();
    CHECK(read.value.table.times(core::StationIndex{0}).size() == 2);
  }
  {
    Editor edit{file.path()};
    edit.put("time", {0, 4}, 4.0);
  }
  const auto e = file.error();
  CHECK(e.code == FormatErrc::padding_not_missing);
  CHECK(e.station == 0);
  CHECK(e.index == 4);
}

// ---- name variables (S2)
// ---------------------------------------------------------

TEST_CASE("probe: a platform_name over the characters alone is not the names",
          "[io][station_nc][foreign][review]") {
  MiniHooks hooks;
  hooks.extra = [](Cdf& f, int /*station*/, int /*time*/, int width) {
    f.var("platform", NC_CHAR, {width});
    f.text("platform", "standard_name", "platform_name");
    f.put_text("platform", "BUOY    ");
  };
  const MiniFile file{hooks};
  const auto read = file.read();
  CHECK(read.value.table.station(core::StationIndex{0}).name.view() == "AAA");
  CHECK(read.value.table.station(core::StationIndex{1}).name.view() == "BBB");
  CHECK(warning_of(read.warnings, WarningCode::skipped_variable).subject ==
        "platform");
}

TEST_CASE(
    "the station name is a platform_name, a variable called station_name, "
    "or the id",
    "[io][station_nc][foreign][review]") {
  const auto with_names = [](const char* name, bool platform) {
    MiniHooks hooks;
    hooks.extra = [=](Cdf& f, int station, int /*time*/, int width) {
      f.var(name, NC_CHAR, {station, width});
      if (platform) {
        f.text(name, "standard_name", "platform_name");
      }
      f.put_rows(name, 8, {"Alpha", "Bravo"});
    };
    return hooks;
  };
  SECTION("a variable called station_name") {
    const MiniFile file{with_names("station_name", false)};
    const auto read = file.read();
    CHECK(read.value.table.station(core::StationIndex{0}).id.view() == "AAA");
    CHECK(read.value.table.station(core::StationIndex{0}).name.view() ==
          "Alpha");
    CHECK(count_of(read.warnings, WarningCode::skipped_variable) == 0);
  }
  SECTION("a platform_name") {
    const MiniFile file{with_names("vessel", true)};
    const auto read = file.read();
    CHECK(read.value.table.station(core::StationIndex{1}).name.view() ==
          "Bravo");
  }
  SECTION("the platform_name wins, the other is skipped") {
    MiniHooks hooks = with_names("vessel", true);
    hooks.extra = [prior = hooks.extra](Cdf& f, int station, int time,
                                        int width) {
      prior(f, station, time, width);
      f.var("station_name", NC_CHAR, {station, width});
      f.put_rows("station_name", 8, {"Wrong", "Names"});
    };
    const MiniFile file{hooks};
    const auto read = file.read();
    CHECK(read.value.table.station(core::StationIndex{0}).name.view() ==
          "Alpha");
    CHECK(warning_of(read.warnings, WarningCode::skipped_variable).subject ==
          "station_name");
  }
  SECTION("the cf_role variable itself called station_name is the id") {
    MiniHooks hooks;
    hooks.id_name = "station_name";
    const MiniFile file{hooks};
    const auto read = file.read();
    CHECK(read.value.table.station(core::StationIndex{0}).id.view() == "AAA");
    CHECK(read.value.table.station(core::StationIndex{0}).name.view() == "AAA");
    CHECK(count_of(read.warnings, WarningCode::skipped_variable) == 0);
  }
}

TEST_CASE("an integer station id that is masked is the index",
          "[io][station_nc][foreign][review]") {
  CfSpec spec;
  spec.integer_ids = true;
  spec.ids = {"101", "-1", "103"};
  spec.customize = [](Cdf& f, int /*station*/, int /*sample*/) {
    f.num("station_name", "_FillValue", NC_INT, {-1.0});
  };
  const auto read = read_spec(spec);
  CHECK(read.value.table.station(core::StationIndex{0}).id.view() == "101");
  CHECK(read.value.table.station(core::StationIndex{1}).id.view() == "1");
  CHECK(warning_of(read.warnings, WarningCode::station_id_substituted).count ==
        1);
}

TEST_CASE("a float station id is bad_encoding",
          "[io][station_nc][foreign][review]") {
  const mov::test::ScratchDir dir;
  const auto path = dir / "float_id.nc";
  {
    Cdf f{path};
    f.text("", "Conventions", "CF-1.8");
    f.text("", "featureType", "timeSeries");
    const int station = f.dim("station", 2);
    const int time = f.dim("time", 2);
    f.var("sid", NC_FLOAT, {station});
    f.text("sid", "cf_role", "timeseries_id");
    f.var("lat", NC_DOUBLE, {station});
    f.text("lat", "units", "degrees_north");
    f.var("lon", NC_DOUBLE, {station});
    f.text("lon", "units", "degrees_east");
    f.var("time", NC_DOUBLE, {time});
    f.text("time", "units", "hours since 2000-01-01");
    f.var("temp", NC_DOUBLE, {station, time});
    f.put("sid", std::vector<double>{1.0, 2.0});
    f.put("lat", std::vector<double>{29.0, 30.0});
    f.put("lon", std::vector<double>{-90.0, -91.0});
    f.put("time", std::vector<double>{0.0, 1.0});
    f.put("temp", std::vector<double>{1.0, 2.0, 3.0, 4.0});
    f.close();
  }
  CHECK(format_error_of(read_all(path)).code == FormatErrc::bad_encoding);
}

// ---- tokens shared with the writer (S3)
// --------------------------------------------

TEST_CASE(
    "probe: a variable and its _status twin are two columns the writer "
    "can write",
    "[io][station_nc][foreign][review]") {
  MiniHooks hooks;
  hooks.extra = [](Cdf& f, int station, int time, int /*width*/) {
    f.var("foo", NC_DOUBLE, {station, time});
    f.text("foo", "units", "m");
    f.var("foo_status", NC_DOUBLE, {station, time});
    f.text("foo_status", "units", "1");
    f.put("foo", std::vector<double>{1.0, 2.0, 3.0, 4.0});
    f.put("foo_status", std::vector<double>{1.0, 2.0, 3.0, 4.0});
  };
  const MiniFile file{hooks};
  const auto read = file.read();
  const core::StationTable& t = read.value.table;
  REQUIRE(t.schema().size() == 3);
  CHECK(core::token(t.schema()[1].quantity()) == "foo");
  CHECK(core::token(t.schema()[2].quantity()) == "foo_status_2");
  CHECK(warning_of(read.warnings, WarningCode::variable_renamed).subject ==
        "foo_status");
  // The point of the rule: what the reader makes, the writer writes.
  const mov::test::ScratchDir out;
  const auto written = write(out / "round.nc", t);
  REQUIRE(written.has_value());
  const auto back = must_read(read_all(out / "round.nc"));
  CHECK(back.value.table.schema().size() == 3);
}

TEST_CASE("a substitute never takes the name a later variable has itself",
          "[io][station_nc][foreign][review]") {
  MiniHooks hooks;
  hooks.extra = [](Cdf& f, int station, int time, int /*width*/) {
    for (const char* name : {"a b", "a_b", "a-b"}) {
      f.var(name, NC_DOUBLE, {station, time});
      f.put(name, std::vector<double>{1.0, 2.0, 3.0, 4.0});
    }
  };
  const MiniFile file{hooks};
  const auto read = file.read();
  const core::StationTable& t = read.value.table;
  REQUIRE(t.schema().size() == 4);
  CHECK(core::token(t.schema()[1].quantity()) == "a_b_2");  // "a b"
  CHECK(core::token(t.schema()[2].quantity()) == "a_b");    // itself
  CHECK(core::token(t.schema()[3].quantity()) == "a_b_3");  // "a-b"
  CHECK(io::validate_station_netcdf(t, {}).has_value());
}

TEST_CASE("a v5 file with a column and the _status of another is refused",
          "[io][station_nc][foreign][review]") {
  CfSpec spec;
  spec.conventions = "CF-1.11";
  spec.customize = [](Cdf& f, int station, int sample) {
    f.text("", "metoceanviewer_format", "station-timeseries");
    f.text("", "metoceanviewer_format_version", "1.0");
    for (const char* name : {"foo", "foo_status"}) {
      f.var(name, NC_DOUBLE, {station, sample});
      f.put(name, std::vector<double>(12, 1.0));
    }
  };
  const auto e = error_of_spec(spec);
  CHECK(e.code == FormatErrc::invalid_variable_name);
  CHECK(e.subject == "foo_status");
}

TEST_CASE("a column's name leaves room for its _status twin (249 bytes)",
          "[io][station_nc][foreign][review]") {
  for (const std::size_t length : {std::size_t{249}, std::size_t{250}}) {
    INFO(length);
    const std::string name(length, 'a');
    CfSpec spec;
    spec.conventions = "CF-1.11";
    spec.customize = [name](Cdf& f, int station, int sample) {
      f.text("", "metoceanviewer_format", "station-timeseries");
      f.text("", "metoceanviewer_format_version", "1.0");
      f.var(name, NC_DOUBLE, {station, sample});
      f.put(name, std::vector<double>(12, 1.0));
    };
    const FixtureFile file{spec};
    if (length == 249) {
      const auto read = file.read();
      CHECK(read.value.table.schema().size() == 2);
    } else {
      CHECK(file.error().code == FormatErrc::invalid_variable_name);
    }
  }
}

// ---- observed and predicted water levels (owner decision 30.4)
// -------------------

namespace {

constexpr const char* level_name = "water_surface_height_above_reference_datum";

/// A file with these variables, each a water level by standard name (in metres)
/// with the long name given (or none).
CfSpec levels(
    std::vector<std::pair<std::string, std::optional<std::string>>> vars) {
  CfSpec spec;
  spec.customize = [vars = std::move(vars)](Cdf& f, int station, int sample) {
    for (const auto& [name, long_name] : vars) {
      f.var(name, NC_DOUBLE, {station, sample});
      f.text(name, "standard_name", level_name);
      f.text(name, "units", "m");
      if (long_name) {
        f.text(name, "long_name", *long_name);
      }
      f.put(name, std::vector<double>(12, 1.0));
    }
  };
  return spec;
}

core::QuantityId level(core::Quantity q) { return core::QuantityId{q}; }

}  // namespace

TEST_CASE("water levels: the hints in the name pick observed or predicted",
          "[io][station_nc][foreign][review]") {
  SECTION("an observation then a prediction") {
    const auto read = read_spec(levels(
        {{"eta", std::nullopt}, {"tide_gauge_prediction", std::nullopt}}));
    const auto& s = read.value.table.schema();
    REQUIRE(s.size() == 3);
    CHECK(s[1].quantity() == level(core::Quantity::water_level));
    CHECK(s[2].quantity() == level(core::Quantity::water_level_prediction));
    CHECK(count_of(read.warnings, WarningCode::unknown_quantity) == 0);
  }
  SECTION("a prediction first: the next one is the observation") {
    const auto read = read_spec(
        levels({{"predicted_eta", std::nullopt}, {"obs", std::nullopt}}));
    const auto& s = read.value.table.schema();
    CHECK(s[1].quantity() == level(core::Quantity::water_level_prediction));
    CHECK(s[2].quantity() == level(core::Quantity::water_level));
  }
  SECTION("the long name hints too") {
    const auto read = read_spec(
        levels({{"wl1", "Water level"}, {"wl2", "Harmonic analysis"}}));
    const auto& s = read.value.table.schema();
    CHECK(s[1].quantity() == level(core::Quantity::water_level));
    CHECK(s[2].quantity() == level(core::Quantity::water_level_prediction));
  }
  SECTION("astronomical and tide are hints as well") {
    const auto read =
        read_spec(levels({{"a", "astronomical tide"}, {"b", std::nullopt}}));
    const auto& s = read.value.table.schema();
    CHECK(s[1].quantity() == level(core::Quantity::water_level_prediction));
    CHECK(s[2].quantity() == level(core::Quantity::water_level));
  }
  SECTION("a third is generic, with its name and a warning") {
    const auto read = read_spec(levels({{"eta", std::nullopt},
                                        {"eta_predicted", std::nullopt},
                                        {"eta3", std::nullopt}}));
    const auto& s = read.value.table.schema();
    REQUIRE(s.size() == 4);
    CHECK(s[3].quantity() == token("eta3", level_name));
    CHECK(warning_of(read.warnings, WarningCode::unknown_quantity).subject ==
          level_name);
  }
  SECTION("two predictions: the second is generic") {
    const auto read =
        read_spec(levels({{"tide_a", std::nullopt}, {"tide_b", std::nullopt}}));
    const auto& s = read.value.table.schema();
    CHECK(s[1].quantity() == level(core::Quantity::water_level_prediction));
    CHECK(s[2].quantity() == token("tide_b", level_name));
  }
}

// ---- datums (owner decision 30.2)
// -------------------------------------------------

namespace {

/// A water level `eta` whose datum is named by the attributes given.
CfSpec datum_spec(const std::optional<std::string>& vertical,
                  const std::optional<std::string>& geopotential,
                  const std::optional<std::string>& mapped = std::nullopt) {
  CfSpec spec;
  spec.customize = [=](Cdf& f, int station, int sample) {
    f.var("eta", NC_DOUBLE, {station, sample});
    f.text("eta", "standard_name", level_name);
    f.text("eta", "units", "m");
    if (vertical) {
      f.text("eta", "vertical_datum", *vertical);
    }
    if (geopotential) {
      f.text("eta", "geopotential_datum_name", *geopotential);
    }
    if (mapped) {
      f.var("crs", NC_INT);
      f.text("crs", "grid_mapping_name", "latitude_longitude");
      f.text("crs", "geopotential_datum_name", *mapped);
      f.text("eta", "grid_mapping", "crs");
    }
    f.put("eta", std::vector<double>(12, 1.0));
  };
  return spec;
}

std::optional<core::VerticalDatum> datum_of(
    const io::Read<io::StationFile>& r) {
  return r.value.table.schema()[1].datum();
}

}  // namespace

TEST_CASE("datums: geopotential_datum_name and vertical_datum name one",
          "[io][station_nc][foreign][review]") {
  SECTION("the CF attribute, by long name") {
    const auto read = read_spec(
        datum_spec(std::nullopt, "North American Vertical Datum of 1988"));
    CHECK(datum_of(read) == core::VerticalDatum::navd88);
    CHECK(count_of(read.warnings, WarningCode::datum_unknown) == 0);
  }
  SECTION("the CF attribute, by abbreviation or other long names") {
    CHECK(datum_of(read_spec(datum_spec(std::nullopt, "MLLW"))) ==
          core::VerticalDatum::mllw);
    CHECK(datum_of(read_spec(datum_spec(std::nullopt, "mean sea level"))) ==
          core::VerticalDatum::msl);
    CHECK(datum_of(read_spec(datum_spec(std::nullopt,
                                        "National Geodetic Vertical Datum of "
                                        "1929"))) ==
          core::VerticalDatum::ngvd29);
    CHECK(
        datum_of(read_spec(datum_spec("Mean Lower Low Water", std::nullopt))) ==
        core::VerticalDatum::mllw);
  }
  SECTION("the grid mapping variable carries it") {
    const auto read =
        read_spec(datum_spec(std::nullopt, std::nullopt, "NAVD88"));
    CHECK(datum_of(read) == core::VerticalDatum::navd88);
  }
  SECTION("vertical_datum wins, then the variable's, then the mapping's") {
    CHECK(datum_of(read_spec(datum_spec("MLLW", "NAVD88", "MSL"))) ==
          core::VerticalDatum::mllw);
    CHECK(datum_of(read_spec(datum_spec(std::nullopt, "NAVD88", "MSL"))) ==
          core::VerticalDatum::navd88);
  }
  SECTION("a datum that is none of ours: no datum, and a warning") {
    const auto read = read_spec(datum_spec(std::nullopt, "Chart Datum 2010"));
    CHECK_FALSE(datum_of(read).has_value());
    CHECK(count_of(read.warnings, WarningCode::datum_unknown) == 1);
  }
  SECTION("the text is not parsed for a datum") {
    const auto read = read_spec(datum_spec("see the comment attribute", {}));
    CHECK_FALSE(datum_of(read).has_value());
    CHECK(count_of(read.warnings, WarningCode::datum_unknown) == 1);
  }
}

// ---- quality flags (owner decision 30.1)
// --------------------------------------------

namespace {

/// The values of the fixture in the order its file holds them, `pad` where
/// the layout has none; `f(station, j)` is the number at each sample.
std::vector<double> arranged(
    const CfSpec& spec,
    const std::function<double(std::size_t, std::size_t)>& f, double pad) {
  const std::size_t stations = spec.ids.size();
  std::size_t longest = 0;
  for (const auto& row : spec.values) {
    longest = std::max(longest, row.size());
  }
  std::vector<double> out;
  switch (spec.kind) {
    case CfKind::orthogonal:
    case CfKind::incomplete:
    case CfKind::single_station: {
      const std::size_t length =
          spec.kind == CfKind::orthogonal ? spec.times.at(0).size() : longest;
      out.assign(stations * length, pad);
      for (std::size_t s = 0; s < stations; ++s) {
        for (std::size_t j = 0; j < spec.values.at(s).size(); ++j) {
          out[spec.transposed ? (j * stations) + s : (s * length) + j] =
              f(s, j);
        }
      }
      break;
    }
    case CfKind::contiguous_ragged:
      for (std::size_t s = 0; s < stations; ++s) {
        for (std::size_t j = 0; j < spec.values.at(s).size(); ++j) {
          out.push_back(f(s, j));
        }
      }
      break;
    case CfKind::indexed_ragged:
      for (std::size_t j = 0; j < longest; ++j) {
        for (std::size_t s = 0; s < stations; ++s) {
          if (j < spec.values.at(s).size()) {
            out.push_back(f(s, j));
          }
        }
      }
      break;
  }
  return out;
}

/// The QARTOD flags of the fixtures: A0 is missing data (9), B1 fails (4), B2
/// (already a missing sample) fails, B3 is suspect (3), the rest pass.
double qartod_flag(std::size_t s, std::size_t j) {
  if (s == 0 and j == 0) {
    return 9;
  }
  if (s == 1 and (j == 1 or j == 2)) {
    return 4;
  }
  return s == 1 and j == 3 ? 3 : 1;
}

struct FlagVariable {
  std::string name{"temperature_qc"};
  nc_type type{NC_BYTE};
  /// The external type of `flag_values`.
  nc_type values_type{NC_BYTE};
  std::optional<std::vector<double>> flag_values;
  std::optional<std::string> flag_meanings;
  std::function<double(std::size_t, std::size_t)> flag{qartod_flag};
};

/// `base` with a flag variable for `temperature`.
CfSpec flagged(CfSpec base, FlagVariable flag) {
  const CfSpec shape = base;
  base.customize = [shape, flag = std::move(flag)](Cdf& f, int station,
                                                   int sample) {
    const bool matrix =
        shape.kind == CfKind::orthogonal or shape.kind == CfKind::incomplete;
    const std::string name = flag.name;
    if (matrix and shape.transposed) {
      f.var(name, flag.type, {sample, station});
    } else if (matrix) {
      f.var(name, flag.type, {station, sample});
    } else {
      f.var(name, flag.type, {sample});
    }
    f.num(name, "_FillValue", flag.type, {-99.0});
    if (flag.flag_values) {
      f.nums(name, "flag_values", flag.values_type, *flag.flag_values);
    }
    if (flag.flag_meanings) {
      f.text(name, "flag_meanings", *flag.flag_meanings);
    }
    f.text("temperature", "ancillary_variables", name);
    f.put(name, arranged(shape, flag.flag, -99.0));
  };
  return base;
}

const std::vector<double> qartod_values{1, 2, 3, 4, 9};
const char* qartod_meanings =
    "pass not_evaluated suspect_or_of_high_interest "
    "fail missing_data";

FlagVariable qartod() {
  FlagVariable flag;
  flag.flag_values = qartod_values;
  flag.flag_meanings = qartod_meanings;
  return flag;
}

CfSpec with_kind(CfKind kind, bool transposed = false) {
  CfSpec spec =
      kind == CfKind::orthogonal ? CfSpec{} : ragged(kind, transposed);
  spec.transposed = transposed;
  return spec;
}

}  // namespace

TEST_CASE(
    "quality flags: QARTOD fail and missing become Missing, suspect is "
    "counted",
    "[io][station_nc][foreign][review]") {
  for (const auto& [kind, transposed] : {std::pair{CfKind::orthogonal, false},
                                         {CfKind::orthogonal, true},
                                         {CfKind::incomplete, false},
                                         {CfKind::incomplete, true},
                                         {CfKind::contiguous_ragged, false},
                                         {CfKind::indexed_ragged, false}}) {
    INFO("layout " << static_cast<int>(kind)
                   << (transposed ? " transposed" : ""));
    const auto read = read_spec(flagged(with_kind(kind, transposed), qartod()));
    const core::StationTable& t = read.value.table;
    REQUIRE(t.schema().size() == 1);   // the flag variable is no series
    CHECK(at(t, 0, 0, 0) == missing);  // flag 9
    CHECK(at(t, 0, 0, 1) == v(21.0));
    CHECK(at(t, 1, 0, 0) == v(30.0));
    CHECK(at(t, 1, 0, 1) == missing);  // flag 4
    CHECK(at(t, 1, 0, 3) == v(33.0));  // flag 3: kept
    CHECK(at(t, 2, 0, 0) == v(40.0));
    const io::Warning masked =
        warning_of(read.warnings, WarningCode::flagged_samples_masked);
    CHECK(masked.subject == "temperature");
    CHECK(masked.count == 2);  // B2 was missing already
    const io::Warning suspect =
        warning_of(read.warnings, WarningCode::suspect_samples_kept);
    CHECK(suspect.subject == "temperature");
    CHECK(suspect.count == 1);
    CHECK(count_of(read.warnings, WarningCode::quality_flags_ignored) == 0);
  }
}

TEST_CASE("quality flags: a selection reads the flags of its stations only",
          "[io][station_nc][foreign][review]") {
  const FixtureFile file{flagged(with_kind(CfKind::indexed_ragged), qartod())};
  const auto selection = core::StationSelection::make({1, 0}, 3);
  REQUIRE(selection.has_value());
  auto read = io::read_station_netcdf(file.path(),
                                      io::StationNcSelection{*selection}, {});
  REQUIRE(read.has_value());
  const core::StationTable& t = read->value.table;
  CHECK(at(t, 0, 0, 1) == missing);  // B1
  CHECK(at(t, 1, 0, 0) == missing);  // A0
  CHECK(warning_of(read->warnings, WarningCode::flagged_samples_masked).count ==
        2);
}

TEST_CASE("quality flags: the meanings decide, not the numbers",
          "[io][station_nc][foreign][review]") {
  SECTION("bad, fail and missing in a meaning mask") {
    FlagVariable flag;
    flag.flag_values = std::vector<double>{0, 1, 2, 3};
    flag.flag_meanings = "good questionable probably_bad MISSING_DATA";
    flag.flag = [](std::size_t s, std::size_t j) {
      return s == 0 ? static_cast<double>(j % 4) : 0.0;
    };
    const auto read = read_spec(flagged(CfSpec{}, flag));
    const core::StationTable& t = read.value.table;
    CHECK(at(t, 0, 0, 0) == v(20.0));
    CHECK(at(t, 0, 0, 1) == v(21.0));  // questionable: kept, not counted
    CHECK(at(t, 0, 0, 2) == missing);
    CHECK(at(t, 0, 0, 3) == missing);
    CHECK(
        warning_of(read.warnings, WarningCode::flagged_samples_masked).count ==
        2);
    CHECK(count_of(read.warnings, WarningCode::suspect_samples_kept) == 0);
  }
  SECTION("a scheme with nothing bad masks nothing and says nothing") {
    FlagVariable flag;
    flag.flag_values = std::vector<double>{0, 1};
    flag.flag_meanings = "dry wet";
    flag.flag = [](std::size_t, std::size_t j) {
      return static_cast<double>(j % 2);
    };
    const auto read = read_spec(flagged(CfSpec{}, flag));
    CHECK(at(read.value.table, 0, 0, 0) == v(20.0));
    CHECK(read.value.table.schema().size() == 1);
    CHECK(count_of(read.warnings, WarningCode::flagged_samples_masked) == 0);
    CHECK(count_of(read.warnings, WarningCode::quality_flags_ignored) == 0);
  }
  SECTION("QARTOD values without meanings are QARTOD") {
    FlagVariable flag;
    flag.flag_values = qartod_values;
    const auto read = read_spec(flagged(CfSpec{}, flag));
    CHECK(at(read.value.table, 0, 0, 0) == missing);
  }
}

TEST_CASE("quality flags: an unknown scheme is ignored, with a warning",
          "[io][station_nc][foreign][review]") {
  const auto ignored = [](const CfSpec& spec) {
    const auto read = read_spec(spec);
    CHECK(read.value.table.schema().size() == 1);
    CHECK(at(read.value.table, 0, 0, 0) == v(20.0));  // nothing masked
    CHECK(count_of(read.warnings, WarningCode::flagged_samples_masked) == 0);
    CHECK(
        warning_of(read.warnings, WarningCode::quality_flags_ignored).subject ==
        "temperature_qc");
    CHECK(count_of(read.warnings, WarningCode::skipped_variable) == 0);
  };
  SECTION("no flag_values") { ignored(flagged(CfSpec{}, FlagVariable{})); }
  SECTION("flag_values that are floats") {
    FlagVariable flag;
    flag.values_type = NC_FLOAT;
    flag.flag_values = std::vector<double>{1, 2, 3, 4, 9};
    flag.flag_meanings = qartod_meanings;
    ignored(flagged(CfSpec{}, flag));
  }
  SECTION("meanings and values that do not match in number") {
    FlagVariable flag;
    flag.flag_values = qartod_values;
    flag.flag_meanings = "pass fail";
    ignored(flagged(CfSpec{}, flag));
  }
  SECTION("values of no scheme, without meanings") {
    FlagVariable flag;
    flag.flag_values = std::vector<double>{0, 5, 7};
    ignored(flagged(CfSpec{}, flag));
  }
}

TEST_CASE("quality flags: a flag variable of two data variables warns once",
          "[io][station_nc][foreign][review]") {
  CfSpec spec = flagged(CfSpec{}, FlagVariable{});
  spec.customize = [prior = spec.customize](Cdf& f, int station, int sample) {
    prior(f, station, sample);
    f.var("second", NC_DOUBLE, {station, sample});
    f.text("second", "ancillary_variables", "temperature_qc");
    f.put("second", std::vector<double>(12, 5.0));
  };
  const auto read = read_spec(spec);
  CHECK(read.value.table.schema().size() == 2);
  CHECK(count_of(read.warnings, WarningCode::quality_flags_ignored) == 1);
}

// ---- the order of the warnings (N9)
// ----------------------------------------------------

TEST_CASE("foreign: the warnings come in the order SN 12 documents",
          "[io][station_nc][foreign][review]") {
  CfSpec spec;
  spec.ids = {"A\xe9", "A\xe9", ""};
  spec.times = {{0, 2, 1, 1}, {0, 2, 1, 1}, {0, 2, 1, 1}};
  spec.values = {{20, 22, 21, 23}, {30, 32, 31, 33}, {40, 42, 41, 43}};
  spec.customize = [](Cdf& f, int station, int sample) {
    const std::vector<double> data(12, 1.0);
    f.var("skipme", NC_INT64, {station, sample});
    f.var("weird", NC_DOUBLE, {station, sample});
    f.text("weird", "units", "frobs");
    f.text("weird", "ancillary_variables", "weird_qc");
    f.put("weird", data);
    f.var("weird_qc", NC_BYTE, {station, sample});
    for (const char* name : {"eta", "eta2"}) {
      f.var(name, NC_DOUBLE, {station, sample});
      f.text(name, "standard_name", level_name);
      f.text(name, "units", "m");
      f.put(name, data);
    }
    f.text("eta", "vertical_datum", "no such datum");
    f.var("a b", NC_DOUBLE, {station, sample});
    f.put("a b", data);
    // A known scheme on the temperature, with samples to mask and to count.
    f.var("temperature_qc", NC_BYTE, {station, sample});
    f.num("temperature_qc", "flag_values", NC_BYTE, {1, 2, 3, 4, 9});
    f.text("temperature_qc", "flag_meanings",
           "pass not_evaluated suspect fail missing");
    f.text("temperature", "ancillary_variables", "temperature_qc");
    // Rows 0 and 2 of each station: pass, 4 and 3 (the times are unordered
    // in the file, the flags go with their samples).
    f.put("temperature_qc",
          std::vector<double>{1, 4, 3, 1, 1, 4, 3, 1, 1, 4, 3, 1});
  };
  const auto read = read_spec(spec);
  std::vector<WarningCode> codes;
  for (const io::Warning& w : read.warnings) {
    if (codes.empty() or codes.back() != w.code) {
      codes.push_back(w.code);
    }
  }
  const std::vector<WarningCode> documented{
      WarningCode::foreign_cf,
      WarningCode::crs_assumed,
      WarningCode::station_id_substituted,
      WarningCode::invalid_utf8_replaced,
      WarningCode::duplicate_station_id_renamed,
      WarningCode::skipped_variable,
      WarningCode::unknown_quantity,
      WarningCode::variable_renamed,
      WarningCode::unrecognized_unit,
      WarningCode::datum_unknown,
      WarningCode::quality_flags_ignored,
      WarningCode::times_reordered,
      WarningCode::duplicate_times_dropped,
      WarningCode::conflicting_duplicate_times,
      WarningCode::flagged_samples_masked,
      WarningCode::suspect_samples_kept,
  };
  CHECK(codes == documented);
}
