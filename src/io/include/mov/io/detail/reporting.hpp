// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// How every reader reports what it found: a FormatError, the failed result
// that carries an io::Error, and the text of a warning or error subject.
// Private to mov::io (public only because tests include it); no netCDF and no
// Qt, so the text readers use it as the netCDF ones do.

#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "mov/io/error.hpp"

namespace mov::io::detail {

/// A FormatError: `code` about `subject`, at `station` and `index` when the
/// failure has a place.
[[nodiscard]] FormatError format_error(
    FormatErrc code, std::string subject,
    std::optional<std::size_t> station = std::nullopt,
    std::optional<std::size_t> index = std::nullopt);

/// A failed result of any reader: `e`, a narrow error, lifted into io::Error.
/// The one spelling of a failure: `return fail(format_error(...));`.
template <class E>
[[nodiscard]] std::unexpected<Error> fail(E&& e) {
  return std::unexpected{lift<Error>(std::forward<E>(e))};
}

/// Text from a file as a warning or error subject: at most
/// ParseError::max_context_bytes, cut on a UTF-8 boundary.
[[nodiscard]] std::string subject_of(std::string_view text);

}  // namespace mov::io::detail
