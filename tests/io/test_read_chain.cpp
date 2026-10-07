// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The CLI chain read -> convert -> export of core-design.md section 5.0 must
// type-check with exhaustive errors. WP7 supplies the real IMEDS reader, the
// StationTable (WP2) and the CSV writer; here a stub reader, a one-column
// table and a stub exporter stand in, over the real core units and the real
// io::Error, Read and lift. The shape of the chain is the one the design pins.

#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/units.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/warning.hpp"

namespace {

namespace core = mov::core;
namespace io = mov::io;

// Stand-in for core::StationTable: one column of values with an optional unit.
struct StubTable {
  std::vector<double> values;
  std::optional<core::Unit> unit;
};

// Stand-in for io::ImedsFile.
struct StubFile {
  std::string source;
  StubTable table;
};

using CliError = std::variant<io::Error, core::UnitError>;

// Stand-in for read_imeds: the file name picks the outcome.
std::expected<io::Read<StubFile>, io::Error> read_stub(
    const std::filesystem::path& p) {
  const std::string name = p.filename().string();
  if (name == "missing.imeds") {
    return std::unexpected{
        io::Error{io::FileError{.op = io::FileOp::open, .path = p, .ec = {}}}};
  }
  std::optional<core::Unit> unit = core::Unit{core::LengthUnit::meter};
  if (name == "pressure.imeds") {
    unit = core::Unit{core::PressureUnit::pascal};
  } else if (name == "unitless.imeds") {
    unit = std::nullopt;
  }
  return io::Read<StubFile>{
      .value =
          StubFile{.source = "stub",
                   .table = StubTable{.values = {0.3048, 1.0}, .unit = unit}},
      .warnings = {io::Warning{.code = io::WarningCode::header_line_skipped,
                               .subject = name}}};
}

// Stand-in for convert(StationTable, column, unit): the real unit algebra.
std::expected<StubTable, core::UnitError> convert_stub(StubTable t,
                                                       const core::Unit& to) {
  if (not t.unit) {
    return std::unexpected{core::UnitError{core::UnknownUnit{}}};
  }
  const auto affine = core::conversion(*t.unit, to);
  if (not affine) {
    return std::unexpected{core::UnitError{affine.error()}};
  }
  for (double& v : t.values) {
    v = (*affine)(v);
  }
  t.unit = to;
  return t;
}

std::string format_stub(const StubTable& t) {
  std::string out;
  for (const double v : t.values) {
    out += std::format("{:.4f}\n", v);
  }
  return out;
}

// The chain of the design, with the stubs in the places of read_imeds,
// convert and format_csv.
std::expected<io::Read<std::string>, CliError> imeds_to_csv_in_feet(
    const std::filesystem::path& in) {
  return io::and_then_read(
      read_stub(in).transform_error(io::lift<CliError>),
      [](StubFile f) -> std::expected<io::Read<std::string>, CliError> {
        return convert_stub(std::move(f.table),
                            core::Unit{core::LengthUnit::foot})
            .transform([](const StubTable& t) {
              return io::Read<std::string>{.value = format_stub(t),
                                           .warnings = {}};
            })
            .transform_error(io::lift<CliError>);
      });
}

}  // namespace

TEST_CASE("the read -> convert -> export chain has an exhaustive error type",
          "[io][read][chain]") {
  STATIC_REQUIRE(std::same_as<decltype(imeds_to_csv_in_feet({})),
                              std::expected<io::Read<std::string>, CliError>>);
}

TEST_CASE("the chain keeps the reader's warnings in the result",
          "[io][read][chain]") {
  const auto out = imeds_to_csv_in_feet("length.imeds");
  REQUIRE(out.has_value());
  CHECK(out->value == "1.0000\n3.2808\n");
  CHECK(out->warnings ==
        std::vector{io::Warning{.code = io::WarningCode::header_line_skipped,
                                .subject = "length.imeds"}});
}

TEST_CASE("a unit mismatch surfaces as the UnitError alternative",
          "[io][read][chain]") {
  const auto out = imeds_to_csv_in_feet("pressure.imeds");
  REQUIRE(not out.has_value());
  REQUIRE(std::holds_alternative<core::UnitError>(out.error()));
  CHECK(std::holds_alternative<core::IncompatibleUnits>(
      std::get<core::UnitError>(out.error())));

  const auto unknown = imeds_to_csv_in_feet("unitless.imeds");
  REQUIRE(not unknown.has_value());
  const auto* unit_error = std::get_if<core::UnitError>(&unknown.error());
  REQUIRE(unit_error != nullptr);
  CHECK(std::holds_alternative<core::UnknownUnit>(*unit_error));
}

TEST_CASE("a reader error surfaces as the io::Error alternative",
          "[io][read][chain]") {
  const auto out = imeds_to_csv_in_feet("missing.imeds");
  REQUIRE(not out.has_value());
  const auto* io_error = std::get_if<io::Error>(&out.error());
  REQUIRE(io_error != nullptr);
  const auto* file_error = std::get_if<io::FileError>(io_error);
  REQUIRE(file_error != nullptr);
  CHECK(file_error->op == io::FileOp::open);
}
