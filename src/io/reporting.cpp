// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/detail/reporting.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "mov/core/quantity.hpp"
#include "mov/core/units.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/warning.hpp"

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

Read<std::optional<core::Unit>> parsed_unit(
    std::optional<std::string_view> text) {
  Read<std::optional<core::Unit>> out{
      .value = text ? core::parse_unit(*text) : std::nullopt, .warnings = {}};
  const auto* other =
      out.value ? std::get_if<core::OtherUnit>(&*out.value) : nullptr;
  if (other != nullptr and not core::is_canonical_other(*other)) {
    out.warnings.push_back(
        {.code = WarningCode::unrecognized_unit, .subject = subject_of(*text)});
  }
  return out;
}

}  // namespace mov::io::detail
