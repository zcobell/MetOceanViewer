// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <string_view>

#include "mov/io/error.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/io/wire/service.hpp"

/// The checks every response body passes before a parser sees it
/// (docs/providers-design.md section 4.2).
namespace mov::io::wire {

/// The deepest nesting a provider body may have. The deepest real one is 5
/// (a CO-OPS station list: stations, a station, sensors, the sensor list, a
/// sensor).
inline constexpr std::size_t max_json_depth = 16;

/// The JSON nesting pre-scan: one pass counting `[` and `{` outside strings
/// (a backslash escapes the next byte inside one). Nesting deeper than
/// `max_depth` is a ParseError `too_large` at the bracket that went too deep,
/// before the JSON parser sees the text. The parser keeps state for every
/// open level, and a reader one frame per level, so the depth bounds that
/// memory before any of it is spent. Nothing else is checked; text that is
/// not JSON passes and is the parser's to refuse.
[[nodiscard]] std::expected<void, ParseError> check_json_depth(
    std::string_view body, std::size_t max_depth = max_json_depth);

/// The content sniff: the body's first byte that is not white space is `<`,
/// so it is markup (an HTML maintenance or captive-portal page) where JSON or
/// text was expected. Then the result is `unexpected_content` with the
/// Content-Type as its text; otherwise nullopt.
[[nodiscard]] std::optional<ServiceError> sniff_markup(
    Origin origin, std::string_view body, std::string_view content_type);

}  // namespace mov::io::wire
