// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Helpers of the foreign CF station netCDF tests: the fixtures of
// support/foreign_fixtures.hpp read back, and the table the ragged fixtures
// must give whatever their layout.
#pragma once

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <variant>
#include <vector>

#include "foreign_fixtures.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/station_table.hpp"
#include "mov/io/station_netcdf.hpp"
#include "mov/test/scratch_dir.hpp"
#include "station_nc_support.hpp"

namespace mov::test::foreign {

using namespace mov::test::snc;  // NOLINT(google-build-using-namespace)
namespace gen = mov::test::ncgen;

constexpr std::int64_t hour_ms = 3600000;
constexpr std::int64_t base_ms = 946684800000;  // 2000-01-01T00:00:00Z

/// `h` hours after 2000-01-01T00:00:00Z.
[[nodiscard]] inline core::Time hours(double h) {
  return ms(base_ms +
            static_cast<std::int64_t>(h * static_cast<double>(hour_ms)));
}

[[nodiscard]] inline core::QuantityId token(std::string_view name,
                                            std::string_view standard = "") {
  return generic(name, standard);
}

[[nodiscard]] inline core::Sample at(const core::StationTable& t,
                                     std::size_t station, std::size_t column,
                                     std::size_t i) {
  return t.column(core::StationIndex{station}, core::ColumnIndex{column})[i];
}

[[nodiscard]] inline io::ForeignCfOrigin origin_of(const io::StationFile& f) {
  const auto* origin = std::get_if<io::ForeignCfOrigin>(&f.origin);
  REQUIRE(origin != nullptr);
  return origin != nullptr
             ? *origin
             : io::ForeignCfOrigin{.layout = io::CfDsgLayout::orthogonal,
                                   .version = {.major = 0, .minor = 0}};
}

/// The stations of the ragged fixtures: lengths 3, 4 and 2.
[[nodiscard]] inline gen::CfSpec ragged(gen::CfKind kind,
                                        bool transposed = false) {
  gen::CfSpec spec;
  spec.kind = kind;
  spec.transposed = transposed;
  spec.times = {{0, 1, 2}, {0, 1, 2, 3}, {5, 6}};
  spec.values = {{20, 21, 22}, {30, 31, gen::cf_missing, 33}, {40, 41}};
  return spec;
}

/// The fixture `spec`, written to a scratch file.
class FixtureFile {
 public:
  explicit FixtureFile(const gen::CfSpec& spec) : path_{dir_ / "foreign.nc"} {
    gen::make_cf(path_, spec);
    configure_projection_database();
  }
  [[nodiscard]] const std::filesystem::path& path() const { return path_; }
  [[nodiscard]] io::Read<io::StationFile> read(
      const io::ReadContext& ctx = {}) const {
    return must_read(read_all(path_, ctx));
  }
  [[nodiscard]] io::FormatError error() const {
    return format_error_of(read_all(path_));
  }
  [[nodiscard]] io::FormatError inspect_error() const {
    return format_error_of(io::inspect_station_netcdf(path_, {}));
  }

 private:
  ScratchDir dir_;
  std::filesystem::path path_;
};

[[nodiscard]] inline io::Read<io::StationFile> read_spec(
    const gen::CfSpec& spec, const io::ReadContext& ctx = {}) {
  return FixtureFile{spec}.read(ctx);
}

[[nodiscard]] inline io::FormatError error_of_spec(const gen::CfSpec& spec) {
  return FixtureFile{spec}.error();
}

/// The result of the ragged fixtures, whatever their layout.
inline void check_ragged_table(const core::StationTable& t) {
  REQUIRE(t.size() == 3);
  REQUIRE(t.schema().size() == 1);
  CHECK(t.schema()[0].quantity() == token("temperature"));
  CHECK(t.schema()[0].unit() == unit("degC"));
  CHECK_FALSE(t.single_axis());
  CHECK(t.station(core::StationIndex{0}).id.view() == "A");
  CHECK(t.station(core::StationIndex{2}).id.view() == "C");

  const auto a = t.times(core::StationIndex{0});
  REQUIRE(a.size() == 3);
  CHECK(a[0] == hours(0));
  CHECK(a[2] == hours(2));
  const auto b = t.times(core::StationIndex{1});
  REQUIRE(b.size() == 4);
  CHECK(b[3] == hours(3));
  const auto c = t.times(core::StationIndex{2});
  REQUIRE(c.size() == 2);
  CHECK(c[0] == hours(5));
  CHECK(c[1] == hours(6));

  CHECK(at(t, 0, 0, 2) == v(22.0));
  CHECK(at(t, 1, 0, 1) == v(31.0));
  CHECK(at(t, 1, 0, 2) == missing);
  CHECK(at(t, 1, 0, 3) == v(33.0));
  CHECK(at(t, 2, 0, 0) == v(40.0));
  CHECK(at(t, 2, 0, 1) == v(41.0));
  CHECK(t.column(core::StationIndex{0}, core::ColumnIndex{0}).size() == 3);
  CHECK(t.column(core::StationIndex{2}, core::ColumnIndex{0}).size() == 2);
}

}  // namespace mov::test::foreign
