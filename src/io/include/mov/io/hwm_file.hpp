// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <expected>
#include <filesystem>
#include <string_view>
#include <vector>

#include "mov/core/hwm.hpp"
#include "mov/core/units.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"

namespace mov::io {

// The high-water-mark file (LF section 6.1): one mark per line, comma
// separated, `lon, lat, ground, observed, modeled[, difference]`.

/// Parses the marks of `text`; `unit` is the unit of the ground, observed and
/// modeled columns (the file does not say; nothing is converted until the
/// statistics ask).
///
///  - Blank lines are skipped (v4 made a zero mark of each, N20).
///  - The first non-blank line is a header, skipped with a
///    `header_line_skipped` warning, when it has a field and none of its
///    fields looks like a number. A first line with any numeric-looking field
///    is data, and a bad one is a ParseError, not a header.
///  - A row has five fields, or six: the sixth, the difference, is ignored
///    (it is recomputed from the others). Fields are trimmed.
///  - The position goes through `Location::make`. The ground and observed
///    elevations go through `core::checked_elevation` (finite, at most 1e4 m
///    in magnitude), the modeled value through `core::model_value`: a value at
///    or below -999 (ADCIRC writes -99999) is `Dry`, any other must pass
///    `checked_elevation`. NaN and infinities are never accepted (the
///    statistics rely on that).
///
/// Errors: `wrong_field_count` (not five or six fields), `bad_number`,
/// `out_of_range` (a number that does not fit a double, a position that is
/// not a Location, an elevation beyond +-1e4 m, or more marks than
/// `ctx.limits.max_elements`), `empty_input` (no mark at all, a header alone
/// included), Cancelled (`ctx.stop`, polled every 1024 rows). The line and the
/// byte column of the field are in the ParseError.
[[nodiscard]] std::expected<Read<std::vector<core::HighWaterMark>>, Error>
parse_hwm_csv(std::string_view text, core::LengthUnit unit,
              const ReadContext& ctx);

/// parse_hwm_csv on a file.
[[nodiscard]] std::expected<Read<std::vector<core::HighWaterMark>>, Error>
read_hwm_csv(const std::filesystem::path& path, core::LengthUnit unit,
             const ReadContext& ctx);

}  // namespace mov::io
