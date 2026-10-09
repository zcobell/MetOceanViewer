// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The hostile-structure set at the level of the station netCDF readers
// (core-design.md 7.3): files whose structure is wrong in
// one way each, written with the raw netCDF-C API. Each is read as a v5 file,
// a foreign CF file and a legacy file where it applies, and gives a specific
// error (or a defined value) through read_station_netcdf and
// inspect_station_netcdf: never a crash, never an allocation sized by the
// file. The structure fuzzer (fuzz_station_netcdf_structure.cpp) covers the
// space around these cases.

#include <netcdf.h>

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "foreign_support.hpp"
#include "legacy_fixtures.hpp"
#include "mov/io/error.hpp"
#include "mov/io/station_netcdf.hpp"
#include "mov/io/warning.hpp"
#include "mov/test/scratch_dir.hpp"
#include "nc_build.hpp"

namespace {

using namespace mov::test::foreign;  // NOLINT(google-build-using-namespace)
namespace core = mov::core;
namespace io = mov::io;
using gen::Cdf;
using gen::CfKind;
using gen::CfSpec;
using io::FormatErrc;
using io::WarningCode;
using mov::test::ScratchDir;

constexpr std::size_t two_to_31 = std::size_t{1} << 31U;
constexpr std::size_t two_to_40 = std::size_t{1} << 40U;

/// `spec` as a v5 file: CF-1.11 and the two format attributes.
CfSpec v5(CfSpec spec) {
  spec.conventions = "CF-1.11";
  auto before = std::move(spec.customize);
  spec.customize = [before = std::move(before)](Cdf& f, int station,
                                                int sample) {
    f.text("", "metoceanviewer_format", "station-timeseries");
    f.text("", "metoceanviewer_format_version", "1.0");
    if (before) {
      before(f, station, sample);
    }
  };
  return spec;
}

CfSpec v5_incomplete() {
  CfSpec spec = ragged(CfKind::incomplete);
  spec.obs_count = std::vector<std::int64_t>{3, 4, 2};
  return v5(std::move(spec));
}

/// The error of `read_station_netcdf` on the file of `spec`, and that
/// `inspect_station_netcdf` gives the same one when it should.
struct Outcome {
  std::optional<io::Error> read;
  std::optional<io::Error> inspect;
};

Outcome run(const std::filesystem::path& path,
            const io::ReadContext& ctx = {}) {
  auto read = io::read_station_netcdf(path, io::AllStations{}, ctx);
  auto inspect = io::inspect_station_netcdf(path, ctx);
  return {.read = read ? std::nullopt : std::optional{read.error()},
          .inspect = inspect ? std::nullopt : std::optional{inspect.error()}};
}

const io::FormatError& format_of(const std::optional<io::Error>& e) {
  REQUIRE(e.has_value());
  const auto* format = e ? std::get_if<io::FormatError>(&*e) : nullptr;
  REQUIRE(format != nullptr);
  static const io::FormatError none{.code = FormatErrc::not_this_format,
                                    .subject = {}};
  return format != nullptr ? *format : none;
}

const io::NcError& nc_of(const std::optional<io::Error>& e) {
  REQUIRE(e.has_value());
  const auto* nc = e ? std::get_if<io::NcError>(&*e) : nullptr;
  REQUIRE(nc != nullptr);
  static const io::NcError none{.status = io::WrapperFault::closed,
                                .op = io::NcOp::open,
                                .object = {},
                                .file = {}};
  return nc != nullptr ? *nc : none;
}

bool too_large(const io::NcError& e) {
  return e.status == io::NcStatus{io::WrapperFault::too_large};
}

/// A file with a station dimension of 2^31 and chunked variables over it, so
/// no data is written; `flavor` adds the attributes of a v5 file.
void make_huge_stations(const std::filesystem::path& path, bool v5_format) {
  Cdf f{path};
  f.text("", "Conventions", v5_format ? "CF-1.11" : "CF-1.8");
  f.text("", "featureType", "timeSeries");
  if (v5_format) {
    f.text("", "metoceanviewer_format", "station-timeseries");
    f.text("", "metoceanviewer_format_version", "1.0");
  }
  const int station = f.dim("station", two_to_31);
  const int time = f.dim("time", 2);
  const int len = f.dim("len", 1);
  f.chunked("station_name", NC_CHAR, {station, len}, {4096, 1});
  f.text("station_name", "cf_role", "timeseries_id");
  for (const char* name : {"lat", "lon"}) {
    f.chunked(name, NC_DOUBLE, {station}, {4096});
  }
  f.text("lat", "units", "degrees_north");
  f.text("lon", "units", "degrees_east");
  f.var("time", NC_DOUBLE, {time});
  f.text("time", "units", "days since 2000-01-01");
  f.chunked("data", NC_DOUBLE, {station, time}, {4096, 2});
  f.close();
}

/// A file whose sample dimension is 2^40 and whose variables over it are
/// chunked: orthogonal, or incomplete with `obs_count`.
void make_huge_samples(const std::filesystem::path& path, bool incomplete,
                       bool v5_format, bool with_count = true) {
  Cdf f{path};
  f.text("", "Conventions", v5_format ? "CF-1.11" : "CF-1.8");
  f.text("", "featureType", "timeSeries");
  if (v5_format) {
    f.text("", "metoceanviewer_format", "station-timeseries");
    f.text("", "metoceanviewer_format_version", "1.0");
  }
  const int station = f.dim("station", 2);
  const int sample = f.dim(incomplete ? "obs" : "time", two_to_40);
  const int len = f.dim("len", 2);
  f.var("station_name", NC_CHAR, {station, len});
  f.text("station_name", "cf_role", "timeseries_id");
  for (const char* name : {"lat", "lon"}) {
    f.var(name, NC_DOUBLE, {station});
  }
  f.text("lat", "units", "degrees_north");
  f.text("lon", "units", "degrees_east");
  if (incomplete) {
    f.chunked("time", NC_DOUBLE, {station, sample}, {1, 4096});
    f.chunked("data", NC_DOUBLE, {station, sample}, {1, 4096});
    if (with_count) {
      f.var("obs_count", NC_INT, {station});
    }
  } else {
    f.chunked("time", NC_DOUBLE, {sample}, {4096});
    f.chunked("data", NC_DOUBLE, {station, sample}, {1, 4096});
  }
  f.text("time", "units", "days since 2000-01-01");
  f.put_rows("station_name", 2, {"A", "B"});
  f.put("lat", std::vector<double>{29.0, 30.0});
  f.put("lon", std::vector<double>{-90.0, -89.0});
  if (incomplete) {
    if (with_count) {
      f.put_i64("obs_count", std::vector<std::int64_t>{2, 1});
    }
    f.put_block("time", 0, 0, 1, 2, std::vector<double>{0.0, 1.0});
    f.put_block("time", 1, 0, 1, 1, std::vector<double>{0.0});
    f.put_block("data", 0, 0, 1, 2, std::vector<double>{1.0, 2.0});
    f.put_block("data", 1, 0, 1, 1, std::vector<double>{3.0});
  }
  f.close();
}

}  // namespace

// ---- huge dimensions on variables that hold no data -------------------------

TEST_CASE("hostile: 2^31 stations, as v5, foreign CF and legacy",
          "[io][station_nc][hostile]") {
  for (const bool v5_format : {true, false}) {
    const ScratchDir dir;
    make_huge_stations(dir / "huge.nc", v5_format);
    const Outcome out = run(dir / "huge.nc");
    INFO((v5_format ? "v5" : "foreign"));
    CHECK(too_large(nc_of(out.read)));
    CHECK(nc_of(out.read).object == "station");
    CHECK(too_large(nc_of(out.inspect)));
  }
  const ScratchDir dir;
  {
    Cdf f{dir / "legacy.nc"};
    const int stations = f.dim("numStations", two_to_31);
    const int len = f.dim("stationNameLen", 1);
    f.chunked("stationXCoordinate", NC_DOUBLE, {stations}, {4096});
    f.chunked("stationYCoordinate", NC_DOUBLE, {stations}, {4096});
    f.chunked("stationName", NC_CHAR, {stations, len}, {4096, 1});
    f.dim("stationLength_0001", 1);
    f.var("time_station_0001", NC_INT64, {0});
    f.close();
  }
  const Outcome out = run(dir / "legacy.nc");
  CHECK(too_large(nc_of(out.read)));
  CHECK(nc_of(out.read).object == "numStations");
  CHECK(too_large(nc_of(out.inspect)));
}

TEST_CASE("hostile: a 2^40 time dimension is too large to read, not to open",
          "[io][station_nc][hostile]") {
  for (const bool v5_format : {true, false}) {
    const ScratchDir dir;
    make_huge_samples(dir / "huge.nc", false, v5_format);
    const Outcome out = run(dir / "huge.nc");
    INFO((v5_format ? "v5" : "foreign"));
    CHECK(too_large(nc_of(out.read)));
    CHECK_FALSE(out.inspect.has_value());  // the catalog reads no samples
  }
}

TEST_CASE("hostile: a 2^40 obs dimension with a few samples per station",
          "[io][station_nc][hostile]") {
  const ScratchDir dir;
  make_huge_samples(dir / "v5.nc", true, true);
  // The boundary check reads each station's samples and one element more.
  auto read = io::read_station_netcdf(dir / "v5.nc", io::AllStations{}, {});
  REQUIRE(read.has_value());
  CHECK(read->value.table.times(core::StationIndex{0}).size() == 2);
  CHECK(read->value.table.times(core::StationIndex{1}).size() == 1);
  // Checking all of the padding would read 2 x 2^40 elements.
  const auto whole = io::read_station_netcdf(
      dir / "v5.nc", io::AllStations{}, {},
      io::StationNcReadOptions{.padding = io::PaddingCheck::whole});
  REQUIRE_FALSE(whole.has_value());
  CHECK(too_large(nc_of(whole.error())));

  // A foreign file with obs_count reads the same way.
  make_huge_samples(dir / "cf_count.nc", true, false);
  const auto counted =
      io::read_station_netcdf(dir / "cf_count.nc", io::AllStations{}, {});
  REQUIRE(counted.has_value());
  CHECK(counted->value.table.times(core::StationIndex{0}).size() == 2);
  // Without it the reader has to look at the times of every station to count
  // them: 2 x 2^40 elements.
  make_huge_samples(dir / "cf.nc", true, false, false);
  const Outcome out = run(dir / "cf.nc");
  CHECK(too_large(nc_of(out.read)));
  CHECK(too_large(nc_of(out.inspect)));
}

TEST_CASE("hostile: stationLength_0001 of 2^40 in a legacy file",
          "[io][station_nc][hostile]") {
  const ScratchDir dir;
  {
    Cdf f{dir / "legacy.nc"};
    const int stations = f.dim("numStations", 1);
    const int len = f.dim("stationNameLen", 4);
    const int length = f.dim("stationLength_0001", two_to_40);
    f.var("stationXCoordinate", NC_DOUBLE, {stations});
    f.var("stationYCoordinate", NC_DOUBLE, {stations});
    f.var("stationName", NC_CHAR, {stations, len});
    f.chunked("time_station_0001", NC_INT64, {length}, {4096});
    f.chunked("data_station_0001", NC_DOUBLE, {length}, {4096});
    f.num("stationXCoordinate", "HorizontalProjectionEPSG", NC_INT, {4326.0});
    f.put("stationXCoordinate", std::vector<double>{-90.0});
    f.put("stationYCoordinate", std::vector<double>{29.0});
    f.put_rows("stationName", 4, {"A"});
    f.close();
  }
  const Outcome out = run(dir / "legacy.nc");
  CHECK(too_large(nc_of(out.read)));
  CHECK_FALSE(out.inspect.has_value());
  auto catalog = io::inspect_station_netcdf(dir / "legacy.nc", {});
  REQUIRE(catalog.has_value());
  CHECK(catalog->value.stations[0].samples == two_to_40);
}

// ---- attributes -------------------------------------------------------------

TEST_CASE("hostile: attributes over 1 MiB", "[io][station_nc][hostile]") {
  const std::string big((std::size_t{1} << 20U) + 1, 'x');
  SECTION("a header attribute") {
    for (const char* name :
         {"Conventions", "featureType", "metoceanviewer_format_version"}) {
      CfSpec spec = v5(CfSpec{});
      spec.customize = [prior = spec.customize, name, &big](Cdf& f, int station,
                                                            int sample) {
        prior(f, station, sample);
        f.text("", name, big);
      };
      const FixtureFile file{spec};
      const Outcome out = run(file.path());
      INFO(name);
      CHECK(too_large(nc_of(out.read)));
      CHECK(too_large(nc_of(out.inspect)));
    }
  }
  SECTION("units of a data variable") {
    CfSpec spec;
    spec.customize = [&big](Cdf& f, int /*station*/, int /*sample*/) {
      f.text("temperature", "units", big);
    };
    const FixtureFile file{spec};
    const Outcome out = run(file.path());
    CHECK(too_large(nc_of(out.read)));
    CHECK(too_large(nc_of(out.inspect)));
  }
  SECTION("an attribute the reader never looks at costs nothing") {
    CfSpec spec;
    spec.customize = [&big](Cdf& f, int /*station*/, int /*sample*/) {
      f.text("temperature", "comment", big);
      f.text("", "history", big);
    };
    CHECK(FixtureFile{spec}.read().value.table.size() == 3);
  }
}

TEST_CASE("hostile: time units of 10 kB", "[io][station_nc][hostile]") {
  CfSpec spec;
  spec.time_units = std::string(10000, 'q') + " since 2000-01-01";
  const FixtureFile file{spec};
  const auto result =
      io::read_station_netcdf(file.path(), io::AllStations{}, {});
  REQUIRE_FALSE(result.has_value());
  const auto* parse = std::get_if<io::ParseError>(&result.error());
  REQUIRE(parse != nullptr);
  CHECK(parse->code() == io::ParseErrc::bad_time_units);
  CHECK(parse->context().size() <= io::ParseError::max_context_bytes);
}

TEST_CASE("hostile: NC_STRING attributes", "[io][station_nc][hostile]") {
  SECTION("Conventions as one NC_STRING reads") {
    CfSpec spec = v5(CfSpec{});
    spec.customize = [prior = spec.customize](Cdf& f, int station, int sample) {
      prior(f, station, sample);
      f.string_att("", "Conventions", "CF-1.11");
    };
    CHECK(FixtureFile{spec}.read().value.table.size() == 3);
  }
  SECTION("Conventions as two NC_STRINGs is not a text") {
    CfSpec spec = v5(CfSpec{});
    spec.customize = [prior = spec.customize](Cdf& f, int station, int sample) {
      prior(f, station, sample);
      const char* both[2] = {"CF-1.11", "ACDD-1.3"};  // NOLINT
      nc_put_att_string(f.ncid(), NC_GLOBAL, "Conventions", 2, both);
    };
    const auto e = FixtureFile{spec}.error();
    CHECK(e.code == FormatErrc::missing_attribute);
    CHECK(e.subject == ":Conventions");
  }
  SECTION("a numeric format attribute is no format") {
    CfSpec spec = v5(CfSpec{});
    spec.customize = [prior = spec.customize](Cdf& f, int station, int sample) {
      prior(f, station, sample);
      f.num("", "metoceanviewer_format_version", NC_INT, {1.0});
    };
    CHECK(FixtureFile{spec}.error().code == FormatErrc::bad_version);
  }
}

// ---- missing data attributes ------------------------------------------------

namespace {

/// netCDF-C refuses to write a _FillValue of another type than its variable's,
/// or with several values, but such files exist (other writers). The attribute
/// is written as `_FillValuX` and renamed in the file's bytes (a classic
/// header holds names as plain text).
CfSpec with_bad_fill(std::function<void(Cdf&)> attribute) {
  CfSpec spec;
  spec.cmode = 0;
  spec.customize = [attribute = std::move(attribute)](Cdf& f, int station,
                                                      int sample) {
    f.var("bad", NC_DOUBLE, {station, sample});
    attribute(f);
    f.put("bad", std::vector<double>(12, 1.0));
  };
  return spec;
}

void rename_fill(const std::filesystem::path& path) {
  std::string bytes = mov::test::read_bytes(path);
  const std::string placeholder = "_FillValuX";
  for (std::size_t at = bytes.find(placeholder); at != std::string::npos;
       at = bytes.find(placeholder, at + 1)) {
    bytes[at + placeholder.size() - 1] = 'e';
  }
  mov::test::write_bytes(path, bytes);
}

}  // namespace

namespace {

/// A file with the data variable `other` next to `temperature`, and whatever
/// `customize` does.
CfSpec with_other(std::function<void(Cdf&)> customize) {
  CfSpec spec;
  spec.customize = [customize = std::move(customize)](Cdf& f, int station,
                                                      int sample) {
    f.var("other", NC_DOUBLE, {station, sample});
    f.put("other", std::vector<double>(12, 2.0));
    customize(f);
  };
  return spec;
}

/// The read and the catalog of `spec` succeed, `skipped` is the one variable
/// the reader skipped for its masking attributes, and `kept` the number of
/// series left.
void expect_skipped(const CfSpec& spec, const std::string& skipped,
                    std::size_t kept) {
  const FixtureFile file{spec};
  rename_fill(file.path());
  INFO(skipped);
  const auto read = must_read(read_all(file.path()));
  CHECK(read.value.table.schema().size() == kept);
  CHECK(warning_of(read.warnings, WarningCode::skipped_variable).subject ==
        skipped);
  const auto catalog = io::inspect_station_netcdf(file.path(), {});
  REQUIRE(catalog.has_value());
  CHECK(catalog->value.schema.size() == kept);
  CHECK(warning_of(catalog->warnings, WarningCode::skipped_variable).subject ==
        skipped);
}

}  // namespace

TEST_CASE(
    "hostile: a data variable whose masking attributes are bad is skipped",
    "[io][station_nc][hostile]") {
  SECTION("a float _FillValue on a double variable") {
    expect_skipped(with_bad_fill([](Cdf& f) {
                     f.num("bad", "_FillValuX", NC_FLOAT, {-1.0});
                   }),
                   "bad:_FillValue", 1);
  }
  SECTION("two values") {
    expect_skipped(with_bad_fill([](Cdf& f) {
                     f.num("bad", "_FillValuX", NC_DOUBLE, {-1.0, -2.0});
                   }),
                   "bad:_FillValue", 1);
  }
  SECTION("a text _FillValue") {
    expect_skipped(
        with_bad_fill([](Cdf& f) { f.text("bad", "_FillValuX", "x"); }),
        "bad:_FillValue", 1);
  }
  SECTION("a valid_range of one value") {
    expect_skipped(with_other([](Cdf& f) {
                     f.num("temperature", "valid_range", NC_DOUBLE, {0.0});
                   }),
                   "temperature:valid_range", 1);
  }
  SECTION("a scale_factor that is text, an add_offset of two values") {
    expect_skipped(
        with_other([](Cdf& f) { f.text("temperature", "scale_factor", "2"); }),
        "temperature:scale_factor", 1);
    expect_skipped(with_other([](Cdf& f) {
                     f.num("temperature", "add_offset", NC_DOUBLE, {1.0, 2.0});
                   }),
                   "temperature:add_offset", 1);
  }
  SECTION("missing_value that no value of the type can equal") {
    expect_skipped(
        [] {
          CfSpec spec;
          spec.customize = [](Cdf& f, int station, int sample) {
            f.var("f", NC_FLOAT, {station, sample});
            f.num("f", "missing_value", NC_DOUBLE, {0.1});
            f.put("f", std::vector<double>(12, 1.0));
          };
          return spec;
        }(),
        "f:missing_value", 1);
  }
  SECTION("short data packed, with a valid_range in the unpacked type") {
    // The reviewer's probe: a float valid_range on a short variable.
    expect_skipped(
        [] {
          CfSpec spec;
          spec.customize = [](Cdf& f, int station, int sample) {
            f.var("good", NC_DOUBLE, {station, sample});
            f.put("good", std::vector<double>(12, 1.0));
            f.var("packed", NC_SHORT, {station, sample});
            f.num("packed", "scale_factor", NC_FLOAT, {0.01});
            f.num("packed", "valid_range", NC_FLOAT, {-5.5, 40.25});
            f.put("packed", std::vector<double>(12, 100.0));
          };
          return spec;
        }(),
        "packed:valid_range", 2);
  }
  SECTION("none left: no data variables") {
    const FixtureFile file{[] {
      CfSpec spec;
      spec.customize = [](Cdf& f, int /*station*/, int /*sample*/) {
        f.num("temperature", "valid_range", NC_DOUBLE, {0.0});
      };
      return spec;
    }()};
    CHECK(file.error().code == FormatErrc::no_data_variables);
    CHECK(file.inspect_error().code == FormatErrc::no_data_variables);
  }
  SECTION("_Unsigned data is skipped, not refused") {
    CfSpec spec;
    spec.customize = [](Cdf& f, int station, int sample) {
      f.var("u", NC_SHORT, {station, sample});
      f.text("u", "_Unsigned", "true");
      f.put("u", std::vector<double>(12, 1.0));
    };
    const auto read = FixtureFile{spec}.read();
    CHECK(read.value.table.schema().size() == 1);
    CHECK(warning_of(read.warnings, WarningCode::skipped_variable).subject ==
          "u");
  }
}

TEST_CASE(
    "hostile: the masking attributes of the time and position variables "
    "stay strict",
    "[io][station_nc][hostile]") {
  for (const char* variable : {"time", "lat"}) {
    INFO(variable);
    CfSpec spec;
    spec.cmode = 0;
    spec.customize = [variable](Cdf& f, int /*station*/, int /*sample*/) {
      f.num(variable, "_FillValuX", NC_FLOAT, {-1.0});
    };
    const FixtureFile file{spec};
    rename_fill(file.path());
    const Outcome out = run(file.path());
    CHECK(nc_of(out.read).status ==
          io::NcStatus{io::WrapperFault::type_mismatch});
    CHECK(nc_of(out.read).object == std::string{variable} + ":_FillValue");
  }
}

TEST_CASE("hostile: a v5 file stays strict about the masking attributes",
          "[io][station_nc][hostile]") {
  SECTION("a float _FillValue on a double variable") {
    const FixtureFile file{v5(with_bad_fill(
        [](Cdf& f) { f.num("bad", "_FillValuX", NC_FLOAT, {-1.0}); }))};
    rename_fill(file.path());
    const Outcome out = run(file.path());
    CHECK(nc_of(out.read).status ==
          io::NcStatus{io::WrapperFault::type_mismatch});
    CHECK(nc_of(out.read).object == "bad:_FillValue");
    // Opening a v5 file does not look at the samples' attributes.
    CHECK_FALSE(out.inspect.has_value());
  }
  SECTION("a valid_range of one value") {
    CfSpec spec;
    spec.customize = [](Cdf& f, int /*station*/, int /*sample*/) {
      f.num("temperature", "valid_range", NC_DOUBLE, {0.0});
    };
    const Outcome out = run(FixtureFile{v5(spec)}.path());
    CHECK(nc_of(out.read).status ==
          io::NcStatus{io::WrapperFault::count_mismatch});
  }
}

TEST_CASE("hostile: a time that is all fill", "[io][station_nc][hostile]") {
  for (const bool v5_format : {true, false}) {
    CfSpec spec;
    spec.customize = [](Cdf& f, int /*station*/, int /*sample*/) {
      f.num("time", "_FillValue", NC_DOUBLE, {-1.0});
    };
    spec.times = {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}};
    const FixtureFile file{v5_format ? v5(spec) : spec};
    const Outcome out = run(file.path());
    INFO((v5_format ? "v5" : "foreign"));
    const io::FormatError& e = format_of(out.read);
    CHECK(e.code == FormatErrc::time_missing);
    CHECK(e.index == 0);
  }
}

// ---- dimensions and shapes
// ---------------------------------------------------

TEST_CASE("hostile: an id dimension of length 0", "[io][station_nc][hostile]") {
  CfSpec spec;
  spec.id_width = 0;  // the unlimited dimension, empty
  spec.ids = {"", "", ""};
  const FixtureFile foreign{spec};
  // Three stations with an empty id: the indices, said once.
  const auto foreign_read = foreign.read();
  CHECK(foreign_read.value.table.station(core::StationIndex{2}).id.view() ==
        "2");
  CHECK(warning_of(foreign_read.warnings, WarningCode::station_id_substituted)
            .count == 3);
  CHECK(io::inspect_station_netcdf(foreign.path(), {}).has_value());

  // A v5 file has no unlimited dimension.
  const Outcome v5_out = run(FixtureFile{v5(spec)}.path());
  CHECK(format_of(v5_out.read).code == FormatErrc::unsupported_layout);
}

TEST_CASE("hostile: the time dimension is not dimension 0",
          "[io][station_nc][hostile]") {
  for (const bool v5_format : {true, false}) {
    CfSpec spec;
    spec.sample_first = true;
    const FixtureFile file{v5_format ? v5(spec) : spec};
    INFO((v5_format ? "v5" : "foreign"));
    CHECK(file.read().value.table.size() == 3);
  }
  CfSpec ragged_spec = ragged(CfKind::contiguous_ragged);
  ragged_spec.sample_first = true;
  check_ragged_table(FixtureFile{ragged_spec}.read().value.table);
}

TEST_CASE("hostile: a one-dimensional id variable",
          "[io][station_nc][hostile]") {
  CfSpec spec;
  spec.flat_ids = true;
  // As a foreign file it says there is one station while lat and lon are over
  // three: no position matches.
  const Outcome foreign = run(FixtureFile{spec}.path());
  CHECK(format_of(foreign.read).code == FormatErrc::missing_variable);
  CHECK(format_of(foreign.read).subject == "latitude");
  // A v5 file must have the id over (station, length).
  const Outcome v5_out = run(FixtureFile{v5(spec)}.path());
  const io::FormatError& e = format_of(v5_out.read);
  CHECK(e.code == FormatErrc::dimension_mismatch);
  CHECK(format_of(v5_out.inspect).code == FormatErrc::dimension_mismatch);
}

TEST_CASE("hostile: obs_count is -1 or past obs", "[io][station_nc][hostile]") {
  for (const std::int64_t bad : {std::int64_t{-1}, std::int64_t{5}}) {
    CfSpec spec = v5_incomplete();
    spec.obs_count = std::vector<std::int64_t>{3, bad, 2};
    const FixtureFile file{spec};
    const Outcome out = run(file.path());
    INFO(bad);
    const io::FormatError& e = format_of(out.read);
    CHECK(e.code == FormatErrc::bad_obs_count);
    CHECK(e.station == 1);
    CHECK(format_of(out.inspect).code == FormatErrc::bad_obs_count);
  }
}

TEST_CASE("hostile: a three-dimensional time", "[io][station_nc][hostile]") {
  for (const bool v5_format : {true, false}) {
    const ScratchDir dir;
    {
      Cdf f{dir / "cube.nc"};
      f.text("", "Conventions", v5_format ? "CF-1.11" : "CF-1.8");
      f.text("", "featureType", "timeSeries");
      if (v5_format) {
        f.text("", "metoceanviewer_format", "station-timeseries");
        f.text("", "metoceanviewer_format_version", "1.0");
      }
      const int station = f.dim("station", 2);
      const int obs = f.dim("obs", 2);
      const int z = f.dim("z", 2);
      const int len = f.dim("len", 1);
      f.var("station_name", NC_CHAR, {station, len});
      f.text("station_name", "cf_role", "timeseries_id");
      f.var("lat", NC_DOUBLE, {station});
      f.text("lat", "units", "degrees_north");
      f.var("lon", NC_DOUBLE, {station});
      f.text("lon", "units", "degrees_east");
      f.var("time", NC_DOUBLE, {station, obs, z});
      f.text("time", "units", "days since 2000-01-01");
      f.var("data", NC_DOUBLE, {station, obs});
      f.var("obs_count", NC_INT, {station});
      f.close();
    }
    const Outcome out = run(dir / "cube.nc");
    INFO((v5_format ? "v5" : "foreign"));
    CHECK(format_of(out.read).code == FormatErrc::unsupported_layout);
    CHECK(format_of(out.read).subject == "time");
    CHECK(format_of(out.inspect).code == FormatErrc::unsupported_layout);
  }
}

TEST_CASE("hostile: data over (time, station) in a v5 file",
          "[io][station_nc][hostile]") {
  CfSpec spec;
  spec.transposed = true;
  const Outcome out = run(FixtureFile{v5(spec)}.path());
  const io::FormatError& e = format_of(out.read);
  CHECK(e.code == FormatErrc::dimension_mismatch);
  CHECK(e.subject == "temperature");
}

TEST_CASE("hostile: 5000 data variables", "[io][station_nc][hostile]") {
  CfSpec spec;
  spec.customize = [](Cdf& f, int station, int sample) {
    const std::vector<double> values(12, 1.0);
    for (int i = 0; i < 5000; ++i) {
      const std::string name = "v" + std::to_string(i);
      f.var(name, NC_DOUBLE, {station, sample});
      f.put(name, values);
    }
  };
  const FixtureFile file{spec};
  const auto read = file.read();
  CHECK(read.value.table.schema().size() == 5001);
  // 5001 columns of 12 samples are over a limit of 50000 elements.
  io::ReadContext ctx;
  ctx.limits.max_elements = 50000;
  const auto limited =
      io::read_station_netcdf(file.path(), io::AllStations{}, ctx);
  REQUIRE_FALSE(limited.has_value());
  CHECK(too_large(nc_of(limited.error())));
}

TEST_CASE("hostile: coordinates that name variables that are not there",
          "[io][station_nc][hostile]") {
  for (const bool v5_format : {true, false}) {
    CfSpec spec;
    spec.customize = [](Cdf& f, int /*station*/, int /*sample*/) {
      f.text("temperature", "coordinates", "time lat lon nothere also_not");
    };
    const FixtureFile file{v5_format ? v5(spec) : spec};
    INFO((v5_format ? "v5" : "foreign"));
    CHECK(file.read().value.table.size() == 3);
  }
}

TEST_CASE(
    "hostile: ancillary_variables, bounds and grid_mapping that point nowhere",
    "[io][station_nc][hostile]") {
  CfSpec spec;
  spec.customize = [](Cdf& f, int /*station*/, int /*sample*/) {
    f.text("temperature", "ancillary_variables", "gone gone_too");
    f.text("temperature", "grid_mapping", "nowhere");
    f.text("time", "bounds", "time_bnds");
  };
  const auto read = FixtureFile{spec}.read();
  CHECK(read.value.table.schema().size() == 1);
  CHECK(count_of(read.warnings, WarningCode::crs_assumed) == 1);
}

// ---- strings ----------------------------------------------------------------

TEST_CASE("hostile: NULL elements of an NC_STRING id variable",
          "[io][station_nc][hostile]") {
  CfSpec spec;
  spec.string_ids = true;
  spec.null_ids = {false, true, false};
  // A NULL string is no id: the station has its index for one, in a foreign
  // file.
  const FixtureFile file{spec};
  const auto read = file.read();
  CHECK(read.value.table.station(core::StationIndex{1}).id.view() == "1");
  CHECK(warning_of(read.warnings, WarningCode::station_id_substituted).count ==
        1);
  CHECK(io::inspect_station_netcdf(file.path(), {}).has_value());
}

TEST_CASE("hostile: station ids with NULs, controls and invalid UTF-8",
          "[io][station_nc][hostile]") {
  CfSpec spec;
  spec.id_width = 8;
  spec.ids = {std::string{"A\0B", 3}, "\x01\x02", "\xff\xfe"};
  const auto read = FixtureFile{spec}.read();
  CHECK(read.value.table.station(core::StationIndex{0}).id.view() == "A");
  CHECK(count_of(read.warnings, WarningCode::invalid_utf8_replaced) == 1);
}

// ---- legacy -----------------------------------------------------------------

TEST_CASE("hostile: HorizontalProjectionEPSG as NC_CHAR",
          "[io][station_nc][hostile]") {
  const ScratchDir dir;
  gen::LegacyNc spec;
  gen::LegacyStation st;
  st.name = "A";
  st.seconds = {0};
  st.values = {1.0};
  spec.stations.push_back(st);
  spec.epsg_as_text = true;
  gen::make_legacy_nc(dir / "legacy.nc", spec);
  const Outcome out = run(dir / "legacy.nc");
  CHECK(nc_of(out.read).status ==
        io::NcStatus{io::WrapperFault::type_mismatch});
  CHECK(nc_of(out.inspect).status ==
        io::NcStatus{io::WrapperFault::type_mismatch});
}

TEST_CASE("hostile: a referenceDate of 1 MiB", "[io][station_nc][hostile]") {
  const ScratchDir dir;
  gen::LegacyNc spec;
  gen::LegacyStation st;
  st.name = "A";
  st.seconds = {0};
  st.values = {1.0};
  spec.stations.push_back(st);
  spec.reference_date = std::string(std::size_t{1} << 20U, '9');
  gen::make_legacy_nc(dir / "limit.nc", spec);
  // Exactly 1 MiB is within the limit; the first 19 characters are no date.
  const auto within =
      io::read_station_netcdf(dir / "limit.nc", io::AllStations{}, {});
  REQUIRE_FALSE(within.has_value());
  CHECK(std::holds_alternative<io::ParseError>(within.error()));
  spec.reference_date = std::string((std::size_t{1} << 20U) + 1, '9');
  gen::make_legacy_nc(dir / "over.nc", spec);
  const Outcome out = run(dir / "over.nc");
  CHECK(too_large(nc_of(out.read)));
}

// ---- ragged -----------------------------------------------------------------

TEST_CASE("hostile: ragged helpers of the wrong shape",
          "[io][station_nc][hostile]") {
  SECTION("a count variable of doubles") {
    const ScratchDir dir;
    {
      Cdf g{dir / "g.nc"};
      const int station = g.dim("station", 2);
      const int obs = g.dim("obs", 3);
      const int len = g.dim("len", 1);
      g.text("", "Conventions", "CF-1.8");
      g.text("", "featureType", "timeSeries");
      g.var("station_name", NC_CHAR, {station, len});
      g.text("station_name", "cf_role", "timeseries_id");
      g.var("lat", NC_DOUBLE, {station});
      g.text("lat", "units", "degrees_north");
      g.var("lon", NC_DOUBLE, {station});
      g.text("lon", "units", "degrees_east");
      g.var("rowSize", NC_DOUBLE, {station});
      g.text("rowSize", "sample_dimension", "obs");
      g.var("time", NC_DOUBLE, {obs});
      g.text("time", "units", "days since 2000-01-01");
      g.var("data", NC_DOUBLE, {obs});
      g.put_rows("station_name", 1, {"A", "B"});
      g.put("lat", std::vector<double>{1, 2});
      g.put("lon", std::vector<double>{1, 2});
      g.put("rowSize", std::vector<double>{2, 1});
      g.close();
    }
    const Outcome out = run(dir / "g.nc");
    CHECK(nc_of(out.read).status ==
          io::NcStatus{io::WrapperFault::type_mismatch});
  }
  SECTION("an instance_dimension that names no dimension") {
    const ScratchDir dir;
    {
      Cdf g{dir / "g.nc"};
      const int obs = g.dim("obs", 3);
      const int station = g.dim("station", 2);
      const int len = g.dim("len", 1);
      g.text("", "Conventions", "CF-1.8");
      g.text("", "featureType", "timeSeries");
      g.var("station_name", NC_CHAR, {station, len});
      g.text("station_name", "cf_role", "timeseries_id");
      g.var("lat", NC_DOUBLE, {station});
      g.text("lat", "units", "degrees_north");
      g.var("lon", NC_DOUBLE, {station});
      g.text("lon", "units", "degrees_east");
      g.var("idx", NC_INT, {obs});
      g.text("idx", "instance_dimension", "no_such_dimension");
      g.var("time", NC_DOUBLE, {obs});
      g.text("time", "units", "days since 2000-01-01");
      g.var("data", NC_DOUBLE, {obs});
      g.close();
    }
    const Outcome out = run(dir / "g.nc");
    const io::FormatError& e = format_of(out.read);
    CHECK(e.code == FormatErrc::missing_dimension);
    CHECK(e.subject == "no_such_dimension");
  }
  SECTION("both kinds of helper for one dimension") {
    const ScratchDir dir;
    {
      Cdf g{dir / "g.nc"};
      const int obs = g.dim("obs", 3);
      const int station = g.dim("station", 2);
      const int len = g.dim("len", 1);
      g.text("", "Conventions", "CF-1.8");
      g.text("", "featureType", "timeSeries");
      g.var("station_name", NC_CHAR, {station, len});
      g.text("station_name", "cf_role", "timeseries_id");
      g.var("lat", NC_DOUBLE, {station});
      g.text("lat", "units", "degrees_north");
      g.var("lon", NC_DOUBLE, {station});
      g.text("lon", "units", "degrees_east");
      g.var("idx", NC_INT, {obs});
      g.text("idx", "instance_dimension", "station");
      g.var("sizes", NC_INT, {station});
      g.text("sizes", "sample_dimension", "obs");
      g.var("time", NC_DOUBLE, {obs});
      g.text("time", "units", "days since 2000-01-01");
      g.var("data", NC_DOUBLE, {obs});
      g.close();
    }
    CHECK(format_of(run(dir / "g.nc").read).code ==
          FormatErrc::unsupported_layout);
  }
}

// ---- not netCDF at all ------------------------------------------------------

TEST_CASE("hostile: files that are not netCDF", "[io][station_nc][hostile]") {
  const ScratchDir dir;
  SECTION("text") {
    mov::test::write_bytes(dir / "text.nc", "hello\nworld\n");
    const Outcome out = run(dir / "text.nc");
    CHECK(nc_of(out.read).op == io::NcOp::open);
    CHECK(nc_of(out.inspect).op == io::NcOp::open);
  }
  SECTION("empty") {
    mov::test::write_bytes(dir / "empty.nc", "");
    CHECK(nc_of(run(dir / "empty.nc").read).op == io::NcOp::open);
  }
  SECTION("a directory") {
    CHECK(nc_of(run(dir.path()).read).op == io::NcOp::open);
  }
  SECTION("a netCDF-4 file cut short") {
    const CfSpec spec;
    const FixtureFile file{spec};
    const std::string bytes = mov::test::read_bytes(file.path());
    mov::test::write_bytes(dir / "cut.nc",
                           std::string_view{bytes}.substr(0, bytes.size() / 2));
    CHECK(nc_of(run(dir / "cut.nc").read).op == io::NcOp::open);
  }
}
