// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <optional>
#include <stdexcept>
#include <utility>

namespace mov::fetch::test {

/// The value of an optional a test expects to be engaged; throws (failing
/// the test case) when it is not.
template <class T>
[[nodiscard]] const T& must(const std::optional<T>& o) {
  if (not o.has_value()) {
    throw std::logic_error{"an expected value is missing"};
  }
  return *o;
}

template <class T>
[[nodiscard]] T must(std::optional<T>&& o) {
  if (not o.has_value()) {
    throw std::logic_error{"an expected value is missing"};
  }
  return *std::move(o);
}

}  // namespace mov::fetch::test
