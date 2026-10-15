// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cassert>
#include <chrono>
#include <compare>
#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "mov/core/station.hpp"
#include "mov/core/tide/licence.hpp"
#include "mov/core/tide/source_token.hpp"

// The sources of a harmonics file (docs/harmonics-json.md section 6).

namespace mov::core::detail {
struct HarmonicsAccess;  // builds the file's records (src/core/tide)
}  // namespace mov::core::detail

namespace mov::core::tide {

/// The source an id names: its namespace. StationId<provider::Harmonics>
/// only holds ids of the `<source>:<local>` grammar, so there always is one.
[[nodiscard]] constexpr SourceToken namespace_of(
    const StationId<provider::Harmonics>& id) noexcept {
  const auto ns = station_namespace(id.value());
  assert(ns.has_value());  // provider::Harmonics::valid_id checked the grammar
  if (not ns) {
    std::unreachable();
  }
  return *ns;
}

class HarmonicsFile;

/// The position of a source in a HarmonicsFile (sorted by id). Only the
/// file makes one.
class SourceIndex {
 public:
  [[nodiscard]] constexpr std::size_t value() const noexcept { return v_; }
  friend constexpr auto operator<=>(SourceIndex, SourceIndex) = default;

 private:
  friend struct detail::HarmonicsAccess;
  explicit constexpr SourceIndex(std::size_t v) noexcept : v_{v} {}
  std::size_t v_;
};
static_assert(std::is_trivially_copyable_v<SourceIndex>);

/// One element of `sources` (docs/harmonics-json.md section 6).
struct Source {
  SourceToken id;
  StationText name;
  std::optional<std::string> url;
  Licence licence;  // the default licence of the source's stations
  std::string attribution;
  std::chrono::sys_days retrieved;
  std::optional<std::string> version;
  std::optional<std::string> sha256;
  std::optional<std::string> notes;
  friend bool operator==(const Source&, const Source&) = default;
};

}  // namespace mov::core::tide
