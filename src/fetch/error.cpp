// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/fetch/error.hpp"

#include <utility>
#include <variant>

#include "mov/fetch/part.hpp"
#include "mov/io/error.hpp"

namespace mov::fetch {

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
