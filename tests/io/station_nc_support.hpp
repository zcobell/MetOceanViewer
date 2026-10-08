// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Helpers shared by the station netCDF tests: table builders, the reads with
// their unwrapping, and raw netCDF-C inspection of what the writer wrote.

#pragma once

#include <netcdf.h>

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "model_nc_support.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/station_netcdf.hpp"
#include "mov/io/warning.hpp"
#include "mov/test/scratch_dir.hpp"
#include "nc_raw.hpp"
#include "station_nc_canonical.hpp"

namespace mov::test::snc {

using core::Sample;
using core::StationTable;
using core::Time;

[[nodiscard]] inline Time ms(std::int64_t t) {
  return Time{std::chrono::milliseconds{t}};
}

[[nodiscard]] inline Sample v(double x) {
  const std::optional<Sample> s = Sample::of(x);
  REQUIRE(s.has_value());
  return s.value_or(Sample{});
}
inline const Sample dry{core::Dry{}};
inline const Sample missing{};

[[nodiscard]] inline core::Unit unit(std::string_view text) {
  const std::optional<core::Unit> u = core::parse_unit(text);
  REQUIRE(u.has_value());
  return u.value_or(core::Unit{});
}

/// Metadata of `q`, with a unit (parse_unit of the text) and a datum if given.
[[nodiscard]] inline core::SeriesMeta meta(
    core::QuantityId q, std::string label,
    std::optional<std::string_view> unit_text = std::nullopt,
    std::optional<core::VerticalDatum> datum = std::nullopt) {
  core::SeriesMeta m = core::SeriesMeta::make(
      {.quantity = std::move(q),
       .label = std::move(label),
       .unit = unit_text ? std::optional{unit(*unit_text)} : std::nullopt});
  if (datum) {
    auto with = m.assume_datum(*datum);
    REQUIRE(with.has_value());
    m = *std::move(with);
  }
  return m;
}

[[nodiscard]] inline core::GenericQuantity generic(std::string_view token,
                                                   std::string_view sn = "") {
  auto q = core::GenericQuantity::parse({.token = token, .standard_name = sn});
  REQUIRE(q.has_value());
  return q.value_or(core::GenericQuantity{});
}

[[nodiscard]] inline core::FileStation station(
    std::string id, std::string name = "A station", double lat = 29.0,
    double lon = -90.0,
    std::optional<core::DataSource> source = core::DataSource::user) {
  auto key = core::StationKey::make(std::move(id));
  auto text = core::StationText::make(std::move(name));
  auto where = core::Location::make({.lat = lat, .lon = lon});
  REQUIRE(key.has_value());
  REQUIRE(text.has_value());
  REQUIRE(where.has_value());
  return {.id = *std::move(key),
          .name = *std::move(text),
          .location = *where,
          .native = std::nullopt,
          .source = source};
}

/// A table: `variables`, the axes, and one station per row.
[[nodiscard]] inline StationTable table(std::vector<core::Variable> variables,
                                        std::vector<core::TimeAxis> axes,
                                        std::vector<core::StationRow> rows) {
  auto t = core::StationTable::make(std::move(variables), std::move(axes),
                                    std::move(rows));
  REQUIRE(t.has_value());
  return *std::move(t);
}

/// One water-level column (metres, MLLW) at stations "S0", "S1", ... each on
/// its own axis, with the samples given.
[[nodiscard]] inline StationTable water_levels(
    std::vector<core::TimeAxis> axes, std::vector<core::Column> columns) {
  std::vector<core::StationRow> rows;
  for (std::size_t i = 0; i < axes.size(); ++i) {
    rows.push_back({.station = station("S" + std::to_string(i)), .axis = i});
  }
  return table({{.meta = meta(core::Quantity::water_level, "water level", "m",
                              core::VerticalDatum::mllw),
                 .per_station = std::move(columns)}},
               std::move(axes), std::move(rows));
}

/// A canonical time 0 (2023-11-14T22:13:20Z) plus k * 6 minutes.
[[nodiscard]] inline Time t(std::int64_t k) {
  return ms(1700000000000 + (k * 360000));
}

/// write_station_netcdf at a fixed creation time.
[[nodiscard]] inline std::expected<io::Read<io::StationNcLayout>, io::Error>
write(const std::filesystem::path& path, const StationTable& t,
      const io::StationNcWriteOptions& options = {}) {
  return io::write_station_netcdf(path, t, options,
                                  station_nc::canonical_now());
}

/// write, which must succeed.
inline io::Read<io::StationNcLayout> must_write(
    const std::filesystem::path& path, const StationTable& t,
    const io::StationNcWriteOptions& options = {}) {
  auto written = write(path, t, options);
  INFO((written ? std::string{} : what(written.error())));
  REQUIRE(written.has_value());
  return *std::move(written);
}

/// read_station_netcdf of every station.
[[nodiscard]] inline std::expected<io::Read<io::StationFile>, io::Error>
read_all(const std::filesystem::path& path, const io::ReadContext& ctx = {}) {
  return io::read_station_netcdf(path, io::AllStations{}, ctx);
}

/// The v5 file of a successful read.
[[nodiscard]] inline io::Read<io::V5StationFile> must_read(
    std::expected<io::Read<io::StationFile>, io::Error> result) {
  INFO((result ? std::string{} : what(result.error())));
  REQUIRE(result.has_value());
  auto* v5 = std::get_if<io::V5StationFile>(&result->value);
  REQUIRE(v5 != nullptr);
  return {.value = std::move(*v5), .warnings = std::move(result->warnings)};
}

/// The FormatError of a failed result; the test fails on success or another
/// kind of error.
template <class T>
[[nodiscard]] io::FormatError format_error_of(
    const std::expected<T, io::Error>& result) {
  REQUIRE(not result.has_value());
  INFO(what(result.error()));
  const auto* format = std::get_if<io::FormatError>(&result.error());
  REQUIRE(format != nullptr);
  return *format;
}

/// How many warnings of `code` there are.
[[nodiscard]] inline std::size_t count_of(const std::vector<io::Warning>& ws,
                                          io::WarningCode code) {
  std::size_t n = 0;
  for (const io::Warning& w : ws) {
    n += w.code == code ? 1 : 0;
  }
  return n;
}

/// The first warning of `code`; the test fails if there is none.
[[nodiscard]] inline io::Warning warning_of(const std::vector<io::Warning>& ws,
                                            io::WarningCode code) {
  for (const io::Warning& w : ws) {
    if (w.code == code) {
      return w;
    }
  }
  FAIL("no warning " << static_cast<int>(code));
  return {};
}

// ---- raw inspection (netCDF-C, independent of mov::io)
// -----------------------

class Inspect {
 public:
  explicit Inspect(const std::filesystem::path& path)
      : file_{path, ncgen::Raw::Open{}} {}

  [[nodiscard]] int id() const { return file_.id(); }
  [[nodiscard]] bool has_var(const char* name) const {
    int varid = 0;
    return nc_inq_varid(file_.id(), name, &varid) == NC_NOERR;
  }
  [[nodiscard]] bool has_dim(const char* name) const {
    int dimid = 0;
    return nc_inq_dimid(file_.id(), name, &dimid) == NC_NOERR;
  }
  [[nodiscard]] std::size_t dim(const char* name) const {
    int dimid = 0;
    ncgen::check(nc_inq_dimid(file_.id(), name, &dimid), name);
    std::size_t len = 0;
    ncgen::check(nc_inq_dimlen(file_.id(), dimid, &len), name);
    return len;
  }
  [[nodiscard]] int varid(const char* name) const { return file_.varid(name); }
  /// The attribute's type and length, or nullopt.
  [[nodiscard]] std::optional<std::pair<nc_type, std::size_t>> att(
      const char* var, const char* name) const {
    nc_type type = NC_NAT;
    std::size_t len = 0;
    if (nc_inq_att(file_.id(), varid(var), name, &type, &len) != NC_NOERR) {
      return std::nullopt;
    }
    return std::pair{type, len};
  }
  /// A text attribute, or nullopt.
  [[nodiscard]] std::optional<std::string> text(const char* var,
                                                const char* name) const {
    const auto shape = att(var, name);
    if (not shape or shape->first != NC_CHAR) {
      return std::nullopt;
    }
    std::string out(shape->second, '\0');
    ncgen::check(nc_get_att_text(file_.id(), varid(var), name, out.data()),
                 name);
    return out;
  }
  [[nodiscard]] std::vector<double> doubles(const char* var) const {
    std::vector<double> out(size(var));
    ncgen::check(nc_get_var_double(file_.id(), varid(var), out.data()), var);
    return out;
  }
  [[nodiscard]] std::vector<int> ints(const char* var) const {
    std::vector<int> out(size(var));
    ncgen::check(nc_get_var_int(file_.id(), varid(var), out.data()), var);
    return out;
  }
  [[nodiscard]] std::vector<signed char> bytes(const char* var) const {
    std::vector<signed char> out(size(var));
    ncgen::check(nc_get_var_schar(file_.id(), varid(var), out.data()), var);
    return out;
  }
  [[nodiscard]] std::size_t size(const char* var) const {
    int ndims = 0;
    ncgen::check(nc_inq_varndims(file_.id(), varid(var), &ndims), var);
    std::vector<int> dims(static_cast<std::size_t>(ndims));
    ncgen::check(nc_inq_vardimid(file_.id(), varid(var), dims.data()), var);
    std::size_t n = 1;
    for (const int d : dims) {
      std::size_t len = 0;
      ncgen::check(nc_inq_dimlen(file_.id(), d, &len), var);
      n *= len;
    }
    return n;
  }
  [[nodiscard]] nc_type type(const char* var) const {
    nc_type t = NC_NAT;
    ncgen::check(nc_inq_vartype(file_.id(), varid(var), &t), var);
    return t;
  }
  [[nodiscard]] std::vector<std::string> dims_of(const char* var) const {
    int ndims = 0;
    ncgen::check(nc_inq_varndims(file_.id(), varid(var), &ndims), var);
    std::vector<int> dims(static_cast<std::size_t>(ndims));
    ncgen::check(nc_inq_vardimid(file_.id(), varid(var), dims.data()), var);
    std::vector<std::string> names;
    for (const int d : dims) {
      std::string name(NC_MAX_NAME + 1, '\0');
      ncgen::check(nc_inq_dimname(file_.id(), d, name.data()), var);
      names.emplace_back(name.c_str());
    }
    return names;
  }
  /// The chunk sizes, or nullopt for contiguous storage.
  [[nodiscard]] std::optional<std::vector<std::size_t>> chunks(
      const char* var) const {
    int storage = 0;
    std::vector<std::size_t> sizes(dims_of(var).size());
    ncgen::check(
        nc_inq_var_chunking(file_.id(), varid(var), &storage, sizes.data()),
        var);
    if (storage != NC_CHUNKED) {
      return std::nullopt;
    }
    return sizes;
  }
  /// {shuffle, deflate, level}.
  [[nodiscard]] std::vector<int> deflate(const char* var) const {
    int shuffle = 0;
    int deflate = 0;
    int level = 0;
    ncgen::check(
        nc_inq_var_deflate(file_.id(), varid(var), &shuffle, &deflate, &level),
        var);
    return {shuffle, deflate, level};
  }
  /// The rows of a 2-D char variable, raw.
  [[nodiscard]] std::vector<std::string> rows(const char* var) const {
    const std::vector<std::string> dims = dims_of(var);
    REQUIRE(dims.size() == 2);
    const std::size_t n = dim(dims[0].c_str());
    const std::size_t len = dim(dims[1].c_str());
    std::string all(n * len, '\0');
    ncgen::check(nc_get_var_text(file_.id(), varid(var), all.data()), var);
    std::vector<std::string> out;
    for (std::size_t i = 0; i < n; ++i) {
      out.push_back(all.substr(i * len, len));
    }
    return out;
  }

 private:
  ncgen::Raw file_;
};

}  // namespace mov::test::snc
