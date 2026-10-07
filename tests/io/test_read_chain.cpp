// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The CLI chain read -> convert -> export of core-design.md section 5.0 must
// type-check with exhaustive errors, and run: the real IMEDS reader, the real
// unit conversion of a StationTable column, the real CSV writer, composed with
// Read, expected and lift.

#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <expected>
#include <filesystem>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/series_ops.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/units.hpp"
#include "mov/io/csv_export.hpp"
#include "mov/io/error.hpp"
#include "mov/io/imeds.hpp"
#include "mov/io/read.hpp"
#include "mov/io/warning.hpp"
#include "mov/test/fixture.hpp"
#include "mov/test/scratch_dir.hpp"

namespace {

namespace core = mov::core;
namespace io = mov::io;

using CliError = std::variant<io::Error, core::UnitError>;

std::expected<io::Read<std::string>, CliError> imeds_to_csv_in_feet(
    const std::filesystem::path& in) {
  return io::and_then_read(
      io::read_imeds(in, io::ReadContext{}).transform_error(io::lift<CliError>),
      [](io::ImedsFile&& f) -> std::expected<io::Read<std::string>, CliError> {
        return core::convert(std::move(f.table), core::ColumnIndex{0},
                             core::Unit{core::LengthUnit::foot})
            .transform([](core::StationTable&& t) {
              return io::Read<std::string>{.value = io::format_csv(t),
                                           .warnings = {}};
            })
            .transform_error(io::lift<CliError>);
      });
}

std::filesystem::path fixture(std::string_view name) {
  return mov::test::fixture(std::string{"io/imeds/"} + std::string{name});
}

}  // namespace

TEST_CASE("the read -> convert -> export chain has an exhaustive error type",
          "[io][read][chain]") {
  STATIC_REQUIRE(std::same_as<decltype(imeds_to_csv_in_feet({})),
                              std::expected<io::Read<std::string>, CliError>>);
}

TEST_CASE("the chain converts and exports an IMEDS file", "[io][read][chain]") {
  const auto out =
      imeds_to_csv_in_feet(fixture("tabs.imeds"));  // 1.5 m, 1.75 m
  REQUIRE(out.has_value());
  CHECK(out->value ==
        "station_id,station_name,time_utc,quantity,value,status,unit,datum\r\n"
        "T1,T1,2020-01-01T00:00:00.000Z,value,4.921260,value,ft,MSL\r\n"
        "T1,T1,2020-01-01T00:06:00.000Z,value,5.741470,value,ft,MSL\r\n");
  CHECK(out->warnings.empty());
}

TEST_CASE("the chain keeps the reader's warnings in the result",
          "[io][read][chain]") {
  const mov::test::ScratchDir dir;
  mov::test::write_bytes(
      dir / "cst.imeds",
      "a\nb\nNOAA CST MSL m\nS 1.0 2.0\n2020 01 01 00 00 00 1.0\n"
      "2020 01 01 00 06 00 -99999\n");
  const auto out = imeds_to_csv_in_feet(dir / "cst.imeds");
  REQUIRE(out.has_value());
  CHECK(out->warnings ==
        std::vector{io::Warning{.code = io::WarningCode::tz_assumed_utc,
                                .subject = "CST"},
                    io::Warning{.code = io::WarningCode::legacy_sentinel_masked,
                                .subject = "",
                                .count = 1}});
  CHECK(out->value.contains(
      "S,S,2020-01-01T00:06:00.000Z,value,,missing,ft,MSL"));
}

TEST_CASE("a column that is not a length surfaces as the UnitError alternative",
          "[io][read][chain]") {
  const auto speed =
      imeds_to_csv_in_feet(fixture("header_tokens.imeds"));  // m s-1
  REQUIRE(not speed.has_value());
  const auto* unit_error = std::get_if<core::UnitError>(&speed.error());
  REQUIRE(unit_error != nullptr);
  CHECK(std::holds_alternative<core::IncompatibleUnits>(*unit_error));

  const auto unitless = imeds_to_csv_in_feet(fixture("mllw_small.imeds"));
  REQUIRE(not unitless.has_value());
  const auto* unknown = std::get_if<core::UnitError>(&unitless.error());
  REQUIRE(unknown != nullptr);
  CHECK(std::holds_alternative<core::UnknownUnit>(*unknown));
}

TEST_CASE("a reader error surfaces as the io::Error alternative",
          "[io][read][chain]") {
  const mov::test::ScratchDir dir;
  const auto missing = imeds_to_csv_in_feet(dir / "missing.imeds");
  REQUIRE(not missing.has_value());
  const auto* io_error = std::get_if<io::Error>(&missing.error());
  REQUIRE(io_error != nullptr);
  const auto* file_error = std::get_if<io::FileError>(io_error);
  REQUIRE(file_error != nullptr);
  CHECK(file_error->op == io::FileOp::open);

  const auto bad = imeds_to_csv_in_feet(fixture("bad_date.imeds"));
  REQUIRE(not bad.has_value());
  const auto* bad_io = std::get_if<io::Error>(&bad.error());
  REQUIRE(bad_io != nullptr);
  CHECK(std::holds_alternative<io::ParseError>(*bad_io));
}
