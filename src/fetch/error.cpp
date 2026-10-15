// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/fetch/error.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "mov/core/ascii.hpp"
#include "mov/fetch/part.hpp"
#include "mov/io/error.hpp"

namespace mov::fetch {

ServiceError ServiceError::make(Origin origin, ServiceErrc code,
                                std::string_view text) {
  return ServiceError{origin, code,
                      std::string{io::detail::truncate_utf8(
                          core::ascii::trim(text), max_text_bytes)}};
}

Error lift_io(io::Error e) {
  return std::visit(
      [](auto&& alternative) -> Error {
        return Error{std::forward<decltype(alternative)>(alternative)};
      },
      std::move(e));
}

Error lift_merge(MergeError e) {
  return std::visit(
      [](auto&& alternative) -> Error {
        return Error{std::forward<decltype(alternative)>(alternative)};
      },
      std::move(e));
}

}  // namespace mov::fetch
