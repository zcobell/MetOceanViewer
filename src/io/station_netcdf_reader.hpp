// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The two halves of the v5 station netCDF reader (docs/station-netcdf.md
// section 12, the v5 column): opening a file (station_netcdf_open.cpp: the
// header, the structure, the CRS, the stations and the schema, all a catalog
// needs) and reading the samples of selected stations
// (station_netcdf_samples.cpp). Private to src/io/.

#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "mov/core/station_table.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/station_netcdf.hpp"

namespace mov::io::detail::station_nc {

/// A data variable and its wet/dry status variable, if it has one.
struct DataVar {
  nc::VarInfo var;
  std::optional<nc::VarInfo> status;
};

/// The time variable, the layout it gives, and obs_count (incomplete).
struct Timing {
  nc::VarInfo time;
  StationNcLayout layout;
  nc::DimInfo sample;  // `time` or `obs`
  std::optional<nc::VarInfo> obs_count;
};

/// The instance variables found by name (SN 4.2).
struct Instances {
  nc::VarInfo name;
  nc::VarInfo lat;
  nc::VarInfo lon;
  std::optional<nc::VarInfo> provider;
};

/// Which variable of the file is what.
struct Structure {
  std::vector<nc::VarInfo> vars;
  nc::DimInfo station;
  nc::VarInfo id;
  Instances instances;
  Timing timing;
  std::vector<DataVar> data;
};

/// An opened file: its structure and the catalog (origin, stations with their
/// sample counts, schema).
struct Opened {
  Structure structure;
  V5Origin origin;
  StationNcCatalog catalog;
};

/// Everything but the samples, validated.
[[nodiscard]] std::expected<Read<Opened>, Error> open_v5(const nc::File& file,
                                                         const StopToken& stop);

/// The table of the stations `selected` (file indices, in the caller's order).
[[nodiscard]] std::expected<Read<core::StationTable>, Error> read_table(
    const nc::File& file, const Opened& opened,
    std::span<const std::size_t> selected, PaddingCheck padding,
    const StopToken& stop);

/// `code` about `subject`, as an io::Error result.
[[nodiscard]] std::unexpected<Error> invalid(
    FormatErrc code, std::string subject,
    std::optional<std::size_t> station = std::nullopt,
    std::optional<std::size_t> index = std::nullopt);

/// Text from a file as a warning or error subject: at most
/// ParseError::max_context_bytes, cut on a UTF-8 boundary.
[[nodiscard]] std::string subject_of(std::string_view text);

}  // namespace mov::io::detail::station_nc
