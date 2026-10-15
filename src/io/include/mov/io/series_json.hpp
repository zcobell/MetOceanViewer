// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Series as JSON, format `metoceanviewer-series` 1.0 (docs/series-json.md):
// the CLI's `--format json` (docs/providers-design.md section 4.7,
// v5-extras #6). Write-only: MetOceanViewer reads series from station
// netCDF, IMEDS and CSV, not from this format.

#pragma once

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include "mov/core/daily_series.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/io/error.hpp"

namespace mov::io {

/// The `format` member of every series file.
inline constexpr std::string_view series_format = "metoceanviewer-series";

/// A station's daily values (decision 34).
using DailyAtStation = core::AtStation<core::FileStation, core::DailySeries>;

/// The table as series JSON (docs/series-json.md section 4): one station per
/// line, in table order, each with one series per schema column, in schema
/// order. A station:
///   {"id","name","provider"?,"lon","lat","series":[...]}
/// `provider` is the FileStation's DataSource token, absent when it has none;
/// `lon` and `lat` are WGS 84 in the shortest text that reads back as the
/// same double (the native point, if any, is not written). A series:
///   {"quantity","label"?,"unit"?,"datum"?,"time":[...],"value":[...],
///    "status"?:[...]}
/// `quantity` is the token, `unit` the udunits text, `datum` the token;
/// `label`, `unit` and `datum` are absent when empty or unset. `time` holds
/// `YYYY-MM-DDThh:mm:ss.sssZ` (UTC; a year outside 0000-9999 with as many
/// digits as it needs), `value` the shortest round-trip number of each Value
/// sample and `null` for Missing and Dry, and `status` ("value", "missing",
/// "dry" per sample) is written only when the series has a Dry sample. The
/// text never fails: every StationText is UTF-8 and every sample finite.
[[nodiscard]] std::string format_series_json(const core::StationTable& table);

/// Daily values (decision 34): each station has one series, with `date`
/// (`YYYY-MM-DD`, the station's local calendar day) in place of `time` and
/// the USGS `statistic` code ("00003") after `datum`. In the given order.
[[nodiscard]] std::string format_series_json(
    std::span<const DailyAtStation> stations);

/// format_series_json, written atomically (write_file_atomic) in 64 KiB
/// chunks, without building the whole text in memory.
[[nodiscard]] std::expected<void, Error> write_series_json(
    const std::filesystem::path& path, const core::StationTable& table);

[[nodiscard]] std::expected<void, Error> write_series_json(
    const std::filesystem::path& path,
    std::span<const DailyAtStation> stations);

}  // namespace mov::io
