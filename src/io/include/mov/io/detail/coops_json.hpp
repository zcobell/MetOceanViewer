// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// What the CO-OPS readers keep of each body (mov/io/detail/json.hpp), and
// the parse behind read_data. Private to mov::io: public only because tests
// include it (they count a parse's events with these schemas).

#pragma once

#include <expected>
#include <string_view>

#include "mov/io/detail/json.hpp"
#include "mov/io/error.hpp"
#include "mov/io/part.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/wire/coops.hpp"

namespace mov::io::detail::coops {

/// A datagetter body of `series`: its data (or predictions) array streamed,
/// each record keeping t, v, s, d, g, f and q.
[[nodiscard]] const json::Keep& data_keep(const wire::coops::Series& series);

/// A station list: the stations streamed, each keeping id, name, lat, lng,
/// greatlakes, details (established, removed) and sensors (at most 256, each
/// keeping name and status).
[[nodiscard]] const json::Keep& station_list_keep();

/// The tide-prediction list: the stations streamed, each keeping id and type.
[[nodiscard]] const json::Keep& prediction_list_keep();

/// datums.json: units, and the datums streamed, each keeping name and value.
[[nodiscard]] const json::Keep& datums_keep();

/// What classify_body reads: error (and its message), errorMsg, message, and
/// whether data, predictions, stations or datums are there (not their
/// contents).
[[nodiscard]] const json::Keep& classify_keep();

/// read_data's parse of a body classify_body called a Payload.
[[nodiscard]] std::expected<Read<Part>, Error> parse_data(
    std::string_view body, const wire::coops::Series& series,
    const ReadContext& ctx);

}  // namespace mov::io::detail::coops
