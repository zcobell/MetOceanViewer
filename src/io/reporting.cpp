// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/detail/reporting.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "mov/io/error.hpp"

namespace mov::io::detail {

FormatError format_error(FormatErrc code, std::string subject,
                         std::optional<std::size_t> station,
                         std::optional<std::size_t> index) {
  return FormatError{.code = code,
                     .subject = std::move(subject),
                     .station = station,
                     .index = index};
}

std::string subject_of(std::string_view text) {
  return std::string{truncate_utf8(text, ParseError::max_context_bytes)};
}

}  // namespace mov::io::detail
