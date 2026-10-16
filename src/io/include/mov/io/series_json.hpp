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
/// line, in table order. A station holds its time axis once and one series
/// per schema column, in schema order:
///   {"id","name","provider"?,"lon","lat","time":[...],"series":[...]}
/// `provider` is the FileStation's DataSource token, absent when it has none;
/// `lon` and `lat` are WGS 84 in the shortest text that reads back as the
/// same double (the native point, if any, is not written); `time` holds
/// `YYYY-MM-DDThh:mm:ss.sssZ` (UTC; a year outside 0000-9999 with as many
/// digits as it needs). A series:
///   {"quantity","label"?,"unit"?,"datum"?,"value":[...],"status"?:[...]}
/// `quantity` is the token, `unit` the udunits text, `datum` the token;
/// `label`, `unit` and `datum` are absent when empty or unset. `value` holds
/// the shortest round-trip number of each Value sample and `null` for
/// Missing and Dry, and `status` ("value", "missing", "dry" per sample) is
/// written only when the series has a Dry sample.
///
/// A label or unit text that is not UTF-8 or holds a NUL is refused before
/// any text is made (`bad_encoding`, subject the quantity token); station
/// texts are StationText and samples finite, so nothing else can fail.
[[nodiscard]] std::expected<std::string, FormatError> format_series_json(
    const core::StationTable& table);

/// Daily values (decision 34): a station has `date` (`YYYY-MM-DD`, its local
/// calendar days) in place of `time`, and its one series the USGS
/// `statistic` code ("00003") after `datum`. In the given order. Refusals as
/// above.
[[nodiscard]] std::expected<std::string, FormatError> format_series_json(
    std::span<const DailyAtStation> stations);

/// format_series_json, written atomically (write_file_atomic) in 64 KiB
/// chunks, without building the whole text in memory. A refusal is the
/// FormatError above, before the file is opened.
[[nodiscard]] std::expected<void, Error> write_series_json(
    const std::filesystem::path& path, const core::StationTable& table);

[[nodiscard]] std::expected<void, Error> write_series_json(
    const std::filesystem::path& path,
    std::span<const DailyAtStation> stations);

}  // namespace mov::io
