// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// detect_file_type: every kind of file mov::io reads is recognized by what is
// in it, with the fixtures of the earlier work packages, and never by its name.

#include <netcdf.h>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "foreign_support.hpp"
#include "legacy_fixtures.hpp"
#include "model_fixtures.hpp"
#include "mov/io/adcirc_netcdf.hpp"
#include "mov/io/error.hpp"
#include "mov/io/file_type.hpp"
#include "mov/io/hwm_file.hpp"
#include "mov/io/imeds.hpp"
#include "mov/test/fixture.hpp"
#include "mov/test/scratch_dir.hpp"
#include "nc_build.hpp"
#include "nc_edit.hpp"
#include "nc_fixtures.hpp"
#include "station_nc_canonical.hpp"
#include "station_nc_support.hpp"

namespace {

using namespace mov::test::snc;  // NOLINT(google-build-using-namespace)
namespace io = mov::io;
namespace gen = mov::test::ncgen;
namespace station_nc = mov::test::station_nc;
using io::FileType;
using mov::test::fixture;
using mov::test::ScratchDir;

FileType detect(const std::filesystem::path& path) {
  const auto type = io::detect_file_type(path);
  REQUIRE(type.has_value());
  return type.value_or(FileType::unknown);
}

void copy_bytes(const std::filesystem::path& from,
                const std::filesystem::path& to, std::size_t keep = SIZE_MAX) {
  const std::string bytes = mov::test::read_bytes(from);
  mov::test::write_bytes(to, std::string_view{bytes}.substr(0, keep));
}

}  // namespace

TEST_CASE("detect: netCDF files by their attributes and variables",
          "[io][file_type]") {
  const ScratchDir dir;
  SECTION("a v5 station file") {
    const auto path = station_nc::write(station_nc::orthogonal(), dir.path());
    CHECK(detect(path) == FileType::station_netcdf);
    CHECK(detect(station_nc::write(station_nc::incomplete(), dir.path())) ==
          FileType::station_netcdf);
  }
  SECTION("a foreign CF timeSeries file, in every layout") {
    for (const auto kind :
         {gen::CfKind::orthogonal, gen::CfKind::incomplete,
          gen::CfKind::contiguous_ragged, gen::CfKind::indexed_ragged,
          gen::CfKind::single_station}) {
      gen::CfSpec spec;
      spec.kind = kind;
      if (kind == gen::CfKind::single_station) {
        spec.ids = {"S"};
        spec.times = {{0, 1}};
        spec.values = {{1, 2}};
      } else if (kind != gen::CfKind::orthogonal) {
        spec = mov::test::foreign::ragged(kind);
      }
      gen::make_cf(dir / "cf.nc", spec);
      CHECK(detect(dir / "cf.nc") == FileType::foreign_cf_netcdf);
    }
  }
  SECTION("a legacy v4 station file, of either dialect and numbering") {
    gen::LegacyNc spec;
    gen::LegacyStation st;
    st.name = "A";
    st.id = "A";
    st.seconds = {0};
    st.values = {1.0};
    spec.stations.push_back(st);
    gen::make_legacy_nc(dir / "legacy.nc", spec);
    CHECK(detect(dir / "legacy.nc") == FileType::legacy_station_netcdf);
    spec.write_station_id = false;
    spec.write_fileformat = false;
    gen::make_legacy_nc(dir / "legacy_b.nc", spec);
    CHECK(detect(dir / "legacy_b.nc") == FileType::legacy_station_netcdf);
    spec.width = 6;
    gen::make_legacy_nc(dir / "legacy_6.nc", spec);
    CHECK(detect(dir / "legacy_6.nc") == FileType::legacy_station_netcdf);
  }
  SECTION("ADCIRC netCDF output") {
    gen::make_adcirc_nc(dir / "fort.61.nc", gen::AdcircNc{});
    CHECK(detect(dir / "fort.61.nc") == FileType::adcirc_netcdf);
  }
  SECTION("a D-Flow FM history file") {
    gen::make_dflow_nc(dir / "his.nc", gen::DflowNc{});
    CHECK(detect(dir / "his.nc") == FileType::dflow_netcdf);
  }
  SECTION("CRMS (dialect C) is none of them") {
    gen::make_crms_nc(dir / "crms.nc");
    CHECK(detect(dir / "crms.nc") == FileType::unknown);
  }
  SECTION("a netCDF file of another kind") {
    gen::make_typed(dir / "typed.nc");
    CHECK(detect(dir / "typed.nc") == FileType::unknown);
  }
}

TEST_CASE("detect: the order of the netCDF tests", "[io][file_type]") {
  const ScratchDir dir;
  SECTION("ADCIRC output that carries CF attributes stays ADCIRC") {
    gen::make_adcirc_nc(dir / "fort.61.nc", gen::AdcircNc{});
    {
      gen::Editor edit{dir / "fort.61.nc"};
      edit.text("", "Conventions", "CF-1.6");
      edit.text("", "featureType", "timeSeries");
    }
    CHECK(detect(dir / "fort.61.nc") == FileType::adcirc_netcdf);
  }
  SECTION("a D-Flow FM file that carries CF attributes stays D-Flow") {
    gen::make_dflow_nc(dir / "his.nc", gen::DflowNc{});
    {
      gen::Editor edit{dir / "his.nc"};
      edit.text("", "Conventions", "CF-1.6");
      edit.text("", "featureType", "timeSeries");
    }
    CHECK(detect(dir / "his.nc") == FileType::dflow_netcdf);
  }
  SECTION("a v5 file without its format attribute is a foreign CF file") {
    const auto path = station_nc::write(station_nc::orthogonal(), dir.path());
    {
      gen::Editor edit{path};
      edit.remove_att("", "metoceanviewer_format");
    }
    CHECK(detect(path) == FileType::foreign_cf_netcdf);
  }
  SECTION("a file naming another format of ours is none") {
    const auto path = station_nc::write(station_nc::orthogonal(), dir.path());
    {
      gen::Editor edit{path};
      edit.text("", "metoceanviewer_format", "station-profile");
    }
    CHECK(detect(path) == FileType::unknown);
  }
  SECTION("Conventions older than CF-1.6, CF-2, another featureType") {
    gen::make_cf(dir / "cf.nc", gen::CfSpec{});
    for (const char* conventions : {"CF-1.5", "CF-2.0", "COARDS"}) {
      {
        gen::Editor edit{dir / "cf.nc"};
        edit.text("", "Conventions", conventions);
      }
      CHECK(detect(dir / "cf.nc") == FileType::unknown);
    }
    {
      gen::Editor edit{dir / "cf.nc"};
      edit.text("", "Conventions", "CF-1.8");
      edit.text("", "featureType", "trajectory");
    }
    CHECK(detect(dir / "cf.nc") == FileType::unknown);
  }
  SECTION("the classic formats are netCDF too") {
    gen::CfSpec spec;
    spec.cmode = 0;  // CDF-1
    gen::make_cf(dir / "classic.nc", spec);
    CHECK(detect(dir / "classic.nc") == FileType::foreign_cf_netcdf);
    spec.cmode = NC_64BIT_OFFSET;
    gen::make_cf(dir / "offset64.nc", spec);
    CHECK(detect(dir / "offset64.nc") == FileType::foreign_cf_netcdf);
  }
}

TEST_CASE("detect: text files by their first lines", "[io][file_type]") {
  SECTION("IMEDS") {
    for (const char* name :
         {"mllw_small.imeds", "msl_small.imeds", "tabs.imeds", "crlf.imeds",
          "bom.imeds", "empty_station.imeds", "header_blank_source.imeds",
          "minimal_crlf_no_final_newline.imeds", "six_field.imeds",
          "header_only.imeds"}) {
      INFO(name);
      CHECK(detect(fixture(std::string{"io/imeds/"} + name)) ==
            FileType::imeds);
    }
  }
  SECTION("ADCIRC ASCII output") {
    for (const char* name :
         {"elevation_small.txt", "velocity_small.txt", "pressure_small.txt",
          "header_three_columns.txt", "legacy/fort.61", "legacy/fort.62",
          "legacy/fort.71", "legacy/fort.72"}) {
      INFO(name);
      CHECK(detect(fixture(std::string{"io/adcirc/"} + name)) ==
            FileType::adcirc_ascii);
    }
  }
  SECTION("a high-water-mark CSV") {
    for (const char* name :
         {"io/hwm/hwm_header.csv", "io/hwm/hwm_blank_line.csv",
          "io/hwm/hwm_bom_crlf.csv", "io/hwm/hwm_dry_marks.csv",
          "core/hwm/hwm_basic.csv", "core/hwm/hwm_ft.csv"}) {
      INFO(name);
      CHECK(detect(fixture(name)) == FileType::hwm_csv);
    }
  }
  SECTION("text that is none of them") {
    for (const char* name :
         {"io/text_file/plain.txt", "io/text_file/empty.txt",
          "io/text_file/with_nul.txt", "io/adcirc/legacy/stations.csv",
          "io/adcirc/stations_names.csv", "io/hwm/hwm_four_columns.csv",
          "io/hwm/hwm_header_only.csv", "io/adcirc/header_only_description.txt",
          "io/imeds/empty.imeds"}) {
      INFO(name);
      CHECK(detect(fixture(name)) == FileType::unknown);
    }
  }
}

TEST_CASE("detect: the file name decides nothing", "[io][file_type]") {
  const ScratchDir dir;
  // Text under the names of every other format.
  for (const char* name : {"data.nc", "fort.61", "x.imeds", "x.csv", "noext"}) {
    copy_bytes(fixture("io/imeds/mllw_small.imeds"), dir / name);
    INFO(name);
    CHECK(detect(dir / name) == FileType::imeds);
    copy_bytes(fixture("io/adcirc/elevation_small.txt"), dir / name);
    CHECK(detect(dir / name) == FileType::adcirc_ascii);
    copy_bytes(fixture("core/hwm/hwm_basic.csv"), dir / name);
    CHECK(detect(dir / name) == FileType::hwm_csv);
  }
  // netCDF under the names of the text formats.
  const auto path = station_nc::write(station_nc::orthogonal(), dir.path());
  for (const char* name : {"x.imeds", "fort.61", "x.csv", "x.txt", "noext"}) {
    copy_bytes(path, dir / name);
    INFO(name);
    CHECK(detect(dir / name) == FileType::station_netcdf);
  }
}

TEST_CASE("detect: files it cannot call anything", "[io][file_type]") {
  const ScratchDir dir;
  SECTION("an empty file") {
    mov::test::write_bytes(dir / "empty", "");
    CHECK(detect(dir / "empty") == FileType::unknown);
  }
  SECTION("binary bytes") {
    mov::test::write_bytes(dir / "binary", std::string("\x00\x01\x02\x03"
                                                       "abc",
                                                       7));
    CHECK(detect(dir / "binary") == FileType::unknown);
  }
  SECTION("a long first line that is not any header") {
    mov::test::write_bytes(dir / "long", std::string(200000, 'x'));
    CHECK(detect(dir / "long") == FileType::unknown);
  }
  SECTION("a text file that was cut inside its last line") {
    // The sniffed prefix is 64 KiB: an IMEDS file of 1 MB is still IMEDS.
    std::string text =
        mov::test::read_bytes(fixture("io/imeds/mllw_small.imeds"));
    while (text.size() < 200000) {
      text += "2015    07    01    00    00    00    2.605\n";
    }
    mov::test::write_bytes(dir / "big.imeds", text);
    CHECK(detect(dir / "big.imeds") == FileType::imeds);
  }
}

TEST_CASE("detect: errors", "[io][file_type]") {
  const ScratchDir dir;
  SECTION("a missing file") {
    const auto type = io::detect_file_type(dir / "nope");
    REQUIRE_FALSE(type.has_value());
    const auto* file = std::get_if<io::FileError>(&type.error());
    REQUIRE(file != nullptr);
    CHECK(file->op == io::FileOp::open);
  }
  SECTION("a directory") {
    const auto type = io::detect_file_type(dir.path());
    REQUIRE_FALSE(type.has_value());
    CHECK(std::holds_alternative<io::FileError>(type.error()));
  }
  SECTION("netCDF magic bytes and nothing else") {
    for (const char* magic : {"CDF\x01", "CDF\x02", "\x89HDF\r\n\x1a\n"}) {
      mov::test::write_bytes(dir / "magic", magic);
      const auto type = io::detect_file_type(dir / "magic");
      REQUIRE_FALSE(type.has_value());
      CHECK(std::holds_alternative<io::NcError>(type.error()));
    }
  }
  SECTION("a netCDF-4 file cut short") {
    const auto path = station_nc::write(station_nc::orthogonal(), dir.path());
    copy_bytes(path, dir / "cut.nc", 600);
    const auto type = io::detect_file_type(dir / "cut.nc");
    REQUIRE_FALSE(type.has_value());
    CHECK(std::holds_alternative<io::NcError>(type.error()));
  }
  SECTION("an attribute over the limit") {
    const auto path = station_nc::write(station_nc::orthogonal(), dir.path());
    io::ReadLimits limits;
    limits.max_att_bytes = 4;  // "station-timeseries" does not fit
    const auto type = io::detect_file_type(path, limits);
    REQUIRE_FALSE(type.has_value());
    const auto* nc = std::get_if<io::NcError>(&type.error());
    REQUIRE(nc != nullptr);
    CHECK(nc->status == io::NcStatus{io::WrapperFault::too_large});
  }
}

TEST_CASE("detect: the tokens", "[io][file_type]") {
  CHECK(io::to_token(FileType::station_netcdf) == "station_netcdf");
  CHECK(io::to_token(FileType::foreign_cf_netcdf) == "foreign_cf_netcdf");
  CHECK(io::to_token(FileType::legacy_station_netcdf) ==
        "legacy_station_netcdf");
  CHECK(io::to_token(FileType::adcirc_netcdf) == "adcirc_netcdf");
  CHECK(io::to_token(FileType::dflow_netcdf) == "dflow_netcdf");
  CHECK(io::to_token(FileType::imeds) == "imeds");
  CHECK(io::to_token(FileType::adcirc_ascii) == "adcirc_ascii");
  CHECK(io::to_token(FileType::hwm_csv) == "hwm_csv");
  CHECK(io::to_token(FileType::unknown) == "unknown");
}

TEST_CASE("detect: the file is read by the reader it was sent to",
          "[io][file_type]") {
  const ScratchDir dir;
  const auto station = station_nc::write(station_nc::incomplete(), dir.path());
  REQUIRE(detect(station) == FileType::station_netcdf);
  CHECK(read_all(station).has_value());

  gen::make_cf(dir / "cf.nc", gen::CfSpec{});
  REQUIRE(detect(dir / "cf.nc") == FileType::foreign_cf_netcdf);
  CHECK(read_all(dir / "cf.nc").has_value());

  gen::LegacyNc legacy;
  gen::LegacyStation st;
  st.name = "A";
  st.seconds = {0, 60};
  st.values = {1.0, 2.0};
  legacy.stations.push_back(st);
  gen::make_legacy_nc(dir / "legacy.nc", legacy);
  REQUIRE(detect(dir / "legacy.nc") == FileType::legacy_station_netcdf);
  CHECK(read_all(dir / "legacy.nc").has_value());

  const auto imeds = fixture("io/imeds/mllw_small.imeds");
  REQUIRE(detect(imeds) == FileType::imeds);
  CHECK(io::read_imeds(imeds, {}).has_value());

  const auto hwm = fixture("core/hwm/hwm_basic.csv");
  REQUIRE(detect(hwm) == FileType::hwm_csv);
  CHECK(io::read_hwm_csv(hwm, mov::core::LengthUnit::meter, {}).has_value());
}
