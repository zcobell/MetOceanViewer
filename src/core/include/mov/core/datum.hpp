// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>

#include "mov/core/detail/ascii.hpp"
#include "mov/core/detail/numeric.hpp"
#include "mov/core/units.hpp"

namespace mov::core {

/// A vertical datum. The enumerators are the SN section 10.2 tokens plus
/// IGLD85; the core enum is authoritative.
enum class VerticalDatum : std::uint8_t {
  mhhw,
  mhw,
  mtl,
  msl,
  mlw,
  mllw,
  navd88,
  ngvd29,
  igld85,
  stnd
};

namespace detail {

inline constexpr std::array<VerticalDatum, 10> all_datums{
    VerticalDatum::mhhw,   VerticalDatum::mhw,    VerticalDatum::mtl,
    VerticalDatum::msl,    VerticalDatum::mlw,    VerticalDatum::mllw,
    VerticalDatum::navd88, VerticalDatum::ngvd29, VerticalDatum::igld85,
    VerticalDatum::stnd};
static_assert(all_datums.size() ==
              static_cast<std::size_t>(VerticalDatum::stnd) + 1);

// Indexed by the enumerator.
inline constexpr std::array<std::string_view, all_datums.size()> datum_tokens{
    "MHHW", "MHW",    "MTL",    "MSL",    "MLW",
    "MLLW", "NAVD88", "NGVD29", "IGLD85", "STND"};

struct DatumAlias {
  std::string_view text;
  VerticalDatum datum;
};

// The NOAA names without the year.
inline constexpr std::array datum_aliases{
    DatumAlias{.text = "NAVD", .datum = VerticalDatum::navd88},
    DatumAlias{.text = "NGVD", .datum = VerticalDatum::ngvd29},
    DatumAlias{.text = "IGLD", .datum = VerticalDatum::igld85},
};

}  // namespace detail

/// The upper-case token: "MLLW", "NAVD88", "STND".
[[nodiscard]] constexpr std::string_view to_string(VerticalDatum d) noexcept {
  return detail::datum_tokens[static_cast<std::size_t>(d)];
}

/// Text that is neither a datum nor "no datum". `text` is the trimmed input
/// and views the argument of parse_vertical_datum, so it lives only as long
/// as that does.
struct UnknownDatum {
  std::string_view text;
  friend constexpr bool operator==(const UnknownDatum&,
                                   const UnknownDatum&) = default;
};

/// Case-insensitive, whitespace-trimmed. Besides the tokens it accepts the
/// aliases NAVD, NGVD and IGLD (the NOAA names without the year). "" and
/// "none" mean there is no datum: an engaged expected holding nullopt.
/// Anything else is not guessed and is an UnknownDatum. MHW is a token (v4
/// could not parse it, N8).
[[nodiscard]] constexpr std::expected<std::optional<VerticalDatum>,
                                      UnknownDatum>
parse_vertical_datum(std::string_view s) noexcept {
  s = detail::trim(s);
  if (s.empty() or detail::equal_ignore_case(s, "none")) {
    return std::optional<VerticalDatum>{};
  }
  const auto token =
      std::ranges::find_if(detail::all_datums, [s](VerticalDatum d) {
        return detail::equal_ignore_case(s, to_string(d));
      });
  if (token != detail::all_datums.end()) {
    return std::optional<VerticalDatum>{*token};
  }
  const auto alias = std::ranges::find_if(
      detail::datum_aliases, [s](const detail::DatumAlias& a) {
        return detail::equal_ignore_case(s, a.text);
      });
  if (alias != detail::datum_aliases.end()) {
    return std::optional<VerticalDatum>{alias->datum};
  }
  return std::unexpected{UnknownDatum{.text = s}};
}

/// The height of one datum above some reference datum.
struct DatumHeight {
  VerticalDatum datum;
  Length height;
  friend constexpr bool operator==(const DatumHeight&,
                                   const DatumHeight&) = default;
};

struct MissingMsl {
  friend constexpr bool operator==(MissingMsl, MissingMsl) = default;
};
struct ConflictingHeight {
  VerticalDatum datum;
  friend constexpr bool operator==(ConflictingHeight,
                                   ConflictingHeight) = default;
};
struct NonFiniteHeight {
  VerticalDatum datum;
  friend constexpr bool operator==(NonFiniteHeight, NonFiniteHeight) = default;
};
using DatumTableError =
    std::variant<MissingMsl, ConflictingHeight, NonFiniteHeight>;

struct MissingOffset {
  VerticalDatum datum;
  friend constexpr bool operator==(MissingOffset, MissingOffset) = default;
};

namespace detail {

constexpr std::size_t datum_count = all_datums.size();

/// Heights above the reference datum, collected row by row.
class DatumSheet {
 public:
  /// The reference is 0 above itself, so a row that says otherwise conflicts.
  explicit constexpr DatumSheet(VerticalDatum reference) noexcept {
    heights_[index(reference)] = 0.0;
  }

  /// Records `metres` for `d`: an error if it is not finite or contradicts an
  /// earlier row (an equal repeat is fine).
  [[nodiscard]] constexpr std::optional<DatumTableError> add(
      VerticalDatum d, double metres) noexcept {
    if (not is_finite(metres)) {
      return DatumTableError{NonFiniteHeight{d}};
    }
    std::optional<double>& slot = heights_[index(d)];
    if (slot and *slot != metres) {
      return DatumTableError{ConflictingHeight{d}};
    }
    slot = metres;
    return std::nullopt;
  }

  /// MSL's height above the reference, which every other datum is rebased on.
  [[nodiscard]] constexpr std::optional<double> msl() const noexcept {
    return heights_[index(VerticalDatum::msl)];
  }

  [[nodiscard]] constexpr std::optional<double> height(
      VerticalDatum d) const noexcept {
    return heights_[index(d)];
  }

 private:
  static constexpr std::size_t index(VerticalDatum d) noexcept {
    return static_cast<std::size_t>(d);
  }

  std::array<std::optional<double>, datum_count> heights_{};
};

}  // namespace detail

/// What a station knows about its datums: heights above MSL. MSL is not
/// stored, it is 0 by definition.
class DatumTable {
 public:
  constexpr DatumTable() noexcept = default;  // knows only MSL

  /// `h` gives heights relative to `reference` (MSL for NOAA CO-OPS, MLLW
  /// for XTide). Equal repeated rows are accepted, conflicting or non-finite
  /// ones are errors (the first in input order is reported), and unless the
  /// reference is MSL, MSL must be among the rows.
  [[nodiscard]] static constexpr std::expected<DatumTable, DatumTableError>
  from_heights(VerticalDatum reference,
               std::span<const DatumHeight> h) noexcept {
    detail::DatumSheet sheet{reference};
    for (const DatumHeight& row : h) {
      if (const auto error =
              sheet.add(row.datum, row.height.as(LengthUnit::meter))) {
        return std::unexpected{*error};
      }
    }
    return rebased_on_msl(sheet);
  }

  /// nullopt if the datum is not in the table; MSL is always 0.
  [[nodiscard]] constexpr std::optional<Length> height_above_msl(
      VerticalDatum d) const noexcept {
    if (d == VerticalDatum::msl) {
      return Length{};
    }
    return heights_[slot(d)];
  }

  /// height[from] - height[to]: the amount added to a series in `from` to
  /// express it in `to`. offset(d, d) is 0 for every d, even an unknown one.
  /// An unknown datum is reported as MissingOffset, `from` first.
  [[nodiscard]] constexpr std::expected<Length, MissingOffset> offset(
      VerticalDatum from, VerticalDatum to) const noexcept {
    if (from == to) {
      return Length{};
    }
    const auto a = height_above_msl(from);
    if (not a) {
      return std::unexpected{MissingOffset{from}};
    }
    const auto b = height_above_msl(to);
    if (not b) {
      return std::unexpected{MissingOffset{to}};
    }
    return *a - *b;
  }

  friend constexpr bool operator==(const DatumTable&,
                                   const DatumTable&) = default;

 private:
  // heights_ holds every datum but MSL, in enumerator order.
  static constexpr std::size_t slot(VerticalDatum d) noexcept {
    const auto i = static_cast<std::size_t>(d);
    return i < static_cast<std::size_t>(VerticalDatum::msl) ? i : i - 1;
  }

  static constexpr std::expected<DatumTable, DatumTableError> rebased_on_msl(
      const detail::DatumSheet& sheet) noexcept {
    const auto msl = sheet.msl();
    if (not msl) {
      return std::unexpected{DatumTableError{MissingMsl{}}};
    }
    DatumTable table;
    for (const VerticalDatum d : detail::all_datums) {
      const auto above_reference = sheet.height(d);
      if (not above_reference or d == VerticalDatum::msl) {
        continue;
      }
      const double above_msl = *above_reference - *msl;
      if (not detail::is_finite(above_msl)) {  // the subtraction overflowed
        return std::unexpected{DatumTableError{NonFiniteHeight{d}}};
      }
      table.heights_[slot(d)] = Length::in(above_msl, LengthUnit::meter);
    }
    return table;
  }

  std::array<std::optional<Length>, detail::datum_count - 1> heights_{};
};

}  // namespace mov::core
