// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// What the USGS readers keep of each body (mov/io/detail/json.hpp), and the
// parses behind the read_* entry points. Private to mov::io: public only
// because tests and fuzz targets include them (they count a parse's events
// with these schemas, and parse bodies without the classification).

#pragma once

#include <cstddef>
#include <expected>
#include <string_view>

#include "mov/core/provider/usgs.hpp"
#include "mov/io/detail/json.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/wire/usgs.hpp"

namespace mov::io::detail::usgs {

/// A data page: the features streamed, each keeping `properties` (time,
/// value, approval_status, qualifier); numberReturned; the links (rel,
/// href).
[[nodiscard]] const json::Keep& data_keep();

/// A combined-metadata page: as data_keep with the series members, and for
/// the catalog the location's id, name and geometry.
[[nodiscard]] const json::Keep& series_keep();

/// What classify_body and classify_error read: `error` (code, message),
/// `code` and `description`.
[[nodiscard]] const json::Keep& classify_keep();

/// The most links a page may list (the API lists 6).
inline constexpr std::size_t max_links = 64;

/// The parses of a Payload.
[[nodiscard]] std::expected<Read<wire::usgs::ContinuousPage>, Error>
parse_continuous(std::string_view body,
                 const core::usgs::Epoch<core::usgs::Continuous>& epoch,
                 const ReadContext& ctx);
[[nodiscard]] std::expected<Read<wire::usgs::DailyPage>, Error> parse_daily(
    std::string_view body, const core::usgs::Epoch<core::usgs::Daily>& epoch,
    const ReadContext& ctx);
[[nodiscard]] std::expected<Read<wire::usgs::SeriesPage>, Error> parse_series(
    std::string_view body, const ReadContext& ctx);
[[nodiscard]] std::expected<Read<wire::usgs::CatalogPage>, Error> parse_catalog(
    std::string_view body, const ReadContext& ctx);

}  // namespace mov::io::detail::usgs
