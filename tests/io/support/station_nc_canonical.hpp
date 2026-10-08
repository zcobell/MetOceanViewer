// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The canonical station netCDF files (docs/station-netcdf.md section 9): the
// tables, options and creation time the golden CDL tests and the
// format-compliance job (tools/check_cf.py) write. One source, so the CI job
// checks exactly the files the goldens pin.

#pragma once

#include <chrono>
#include <filesystem>
#include <string_view>
#include <vector>

#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/io/station_netcdf.hpp"

namespace mov::test::station_nc {

/// One canonical file: its base name (without `.nc`), table and options.
struct Canonical {
  std::string_view name;
  core::StationTable table;
  io::StationNcWriteOptions options;
};

/// 2026-10-06T12:00:00Z, the `date_created` of every canonical file.
[[nodiscard]] std::chrono::sys_seconds canonical_now();

/// SN 9.2: two NOAA stations sharing four times, water level (MLLW, one
/// sample of station 2 dry, so a status variable) and water temperature (all
/// missing at station 2).
[[nodiscard]] Canonical orthogonal();

/// SN 9.1: the same stations with 3 and 5 samples on different axes.
[[nodiscard]] Canonical incomplete();

/// One station and every registry quantity (each in its canonical unit), a
/// `value` column with a datum and a non-canonical unit, a generic quantity
/// with a CF standard name, a generic temperature (`temperature: unknown`) and
/// a `difference`: every standard name, unit and units_metadata the writer can
/// produce, for the checkers.
[[nodiscard]] Canonical registry();

/// All three, in the order above.
[[nodiscard]] std::vector<Canonical> all();

/// Writes `c` to `dir / (c.name + ".nc")` and returns the path; throws
/// std::runtime_error when the writer fails.
std::filesystem::path write(const Canonical& c,
                            const std::filesystem::path& dir);

}  // namespace mov::test::station_nc
