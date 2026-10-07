// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

#include "mov/core/datum.hpp"
#include "mov/core/detail/ascii.hpp"
#include "mov/core/detail/utf8.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/time.hpp"

namespace mov::core {

/// Where a station's data comes from. The tokens are SN's `station_provider`
/// values; this enum is authoritative.
enum class DataSource : std::uint8_t {
  noaa_coops,
  usgs,
  ndbc,
  xtide,
  adcirc,
  dflowfm,
  user
};

namespace detail {

struct DataSourceToken {
  DataSource source;
  std::string_view token;
};

// One row per enumerator, in enumerator order (checked below).
inline constexpr std::array<DataSourceToken, 7> data_source_tokens{{
    {.source = DataSource::noaa_coops, .token = "noaa_coops"},
    {.source = DataSource::usgs, .token = "usgs"},
    {.source = DataSource::ndbc, .token = "ndbc"},
    {.source = DataSource::xtide, .token = "xtide"},
    {.source = DataSource::adcirc, .token = "adcirc"},
    {.source = DataSource::dflowfm, .token = "dflowfm"},
    {.source = DataSource::user, .token = "user"},
}};

[[nodiscard]] constexpr bool rows_in_enumerator_order() noexcept {
  return data_source_tokens.size() ==
             static_cast<std::size_t>(DataSource::user) + 1 and
         std::ranges::all_of(
             std::views::iota(std::size_t{0}, data_source_tokens.size()),
             [](std::size_t i) {
               return static_cast<std::size_t>(data_source_tokens[i].source) ==
                      i;
             });
}
static_assert(rows_in_enumerator_order());

[[nodiscard]] constexpr bool is_digit(char c) noexcept {
  return c >= '0' and c <= '9';
}
[[nodiscard]] constexpr bool is_upper_alnum(char c) noexcept {
  return is_digit(c) or (c >= 'A' and c <= 'Z');
}
[[nodiscard]] constexpr bool is_alnum(char c) noexcept {
  return is_upper_alnum(c) or (c >= 'a' and c <= 'z');
}
[[nodiscard]] constexpr bool is_control(char c) noexcept {
  const auto byte = static_cast<unsigned char>(c);
  return byte < 0x20 or byte == 0x7F;
}

}  // namespace detail

[[nodiscard]] constexpr std::string_view to_token(DataSource s) noexcept {
  return detail::data_source_tokens[static_cast<std::size_t>(s)].token;
}

/// The source with exactly this token (case-sensitive), or nullopt.
[[nodiscard]] constexpr std::optional<DataSource> parse_data_source(
    std::string_view token) noexcept {
  const auto it = std::ranges::find(detail::data_source_tokens, token,
                                    &detail::DataSourceToken::token);
  if (it == detail::data_source_tokens.end()) {
    return std::nullopt;
  }
  return it->source;
}

enum class StationTextError : std::uint8_t { embedded_nul, invalid_utf8 };

/// Text a station file can store (SN 12.4): well-formed UTF-8 without NUL.
/// May be empty. Readers of lenient sources clean the bytes first (C14).
class StationText {
 public:
  constexpr StationText() = default;

  [[nodiscard]] static constexpr std::expected<StationText, StationTextError>
  make(std::string text) {
    if (text.find('\0') != std::string::npos) {
      return std::unexpected{StationTextError::embedded_nul};
    }
    if (not detail::is_valid_utf8(text)) {
      return std::unexpected{StationTextError::invalid_utf8};
    }
    return StationText{std::move(text)};
  }

  [[nodiscard]] constexpr std::string_view view() const& noexcept {
    return text_;
  }
  std::string_view view() const&& = delete;
  [[nodiscard]] constexpr bool empty() const noexcept { return text_.empty(); }

  friend constexpr auto operator<=>(const StationText&,
                                    const StationText&) = default;

 private:
  friend class StationKey;
  explicit constexpr StationText(std::string text) : text_{std::move(text)} {}

  std::string text_;
};

enum class StationKeyError : std::uint8_t { empty, embedded_nul, invalid_utf8 };

/// A station identifier as files store it: non-empty StationText. There is no
/// default (an empty id is not an id).
class StationKey {
 public:
  [[nodiscard]] static constexpr std::expected<StationKey, StationKeyError>
  make(std::string text) {
    // Explicit branches, not expected's monadic operations: GCC 14 cannot
    // evaluate those on a small std::string in a constant expression.
    if (text.empty()) {
      return std::unexpected{StationKeyError::empty};
    }
    if (text.find('\0') != std::string::npos) {
      return std::unexpected{StationKeyError::embedded_nul};
    }
    if (not detail::is_valid_utf8(text)) {
      return std::unexpected{StationKeyError::invalid_utf8};
    }
    return StationKey{StationText{std::move(text)}};
  }

  [[nodiscard]] constexpr std::string_view view() const& noexcept {
    return text_.view();
  }
  std::string_view view() const&& = delete;
  [[nodiscard]] constexpr const StationText& text() const& noexcept {
    return text_;
  }
  const StationText& text() const&& = delete;

  friend constexpr auto operator<=>(const StationKey&,
                                    const StationKey&) = default;

 private:
  explicit constexpr StationKey(StationText t) : text_{std::move(t)} {}

  StationText text_;
};

/// The station providers. Each names its DataSource, how a trimmed raw id is
/// brought to canonical form, and the grammar the canonical id must match.
/// StationId<P>::make is the only way to an id.
namespace provider {

/// NOAA CO-OPS: ^[0-9]{7}$
struct Coops {
  static constexpr DataSource source = DataSource::noaa_coops;
  [[nodiscard]] static constexpr std::string canonical(std::string_view id) {
    return std::string{id};
  }
  [[nodiscard]] static constexpr bool valid_id(std::string_view id) noexcept {
    return id.size() == 7 and std::ranges::all_of(id, detail::is_digit);
  }
};

/// USGS Water Data monitoring location: ^[A-Z0-9]+-[A-Za-z0-9]+$ with the
/// agency prefix upper-cased ("usgs-07374000" -> "USGS-07374000").
struct Usgs {
  static constexpr DataSource source = DataSource::usgs;
  [[nodiscard]] static constexpr std::string canonical(std::string_view id) {
    std::string out{id};
    const auto dash = std::ranges::find(out, '-');
    std::ranges::transform(out.begin(), dash, out.begin(), detail::to_upper);
    return out;
  }
  [[nodiscard]] static constexpr bool valid_id(std::string_view id) noexcept {
    const auto dash = id.find('-');
    if (dash == std::string_view::npos) {
      return false;
    }
    const std::string_view agency = id.substr(0, dash);
    const std::string_view number = id.substr(dash + 1);
    return not agency.empty() and not number.empty() and
           std::ranges::all_of(agency, detail::is_upper_alnum) and
           std::ranges::all_of(number, detail::is_alnum);
  }
};

/// NDBC: ^[A-Z0-9]{5}$, upper-cased.
struct Ndbc {
  static constexpr DataSource source = DataSource::ndbc;
  [[nodiscard]] static constexpr std::string canonical(std::string_view id) {
    std::string out{id};
    std::ranges::transform(out, out.begin(), detail::to_upper);
    return out;
  }
  [[nodiscard]] static constexpr bool valid_id(std::string_view id) noexcept {
    return id.size() == 5 and std::ranges::all_of(id, detail::is_upper_alnum);
  }
};

/// XTide: the station name. Non-empty well-formed UTF-8, at most 255 bytes,
/// no control characters (bytes below 0x20 and 0x7F).
struct Xtide {
  static constexpr DataSource source = DataSource::xtide;
  static constexpr std::size_t max_id_bytes = 255;
  [[nodiscard]] static constexpr std::string canonical(std::string_view id) {
    return std::string{id};
  }
  [[nodiscard]] static constexpr bool valid_id(std::string_view id) noexcept {
    return not id.empty() and id.size() <= max_id_bytes and
           std::ranges::none_of(id, detail::is_control) and
           detail::is_valid_utf8(id);
  }
};

}  // namespace provider

template <class P>
concept Provider = requires(std::string_view s) {
  { P::source } -> std::convertible_to<DataSource>;
  { P::canonical(s) } -> std::same_as<std::string>;
  { P::valid_id(s) } -> std::same_as<bool>;
};

enum class StationIdError : std::uint8_t {
  empty,    // nothing but whitespace
  invalid,  // fails the provider's id grammar (or is not a StationKey)
};

/// A station id of one provider. Passing a USGS id where a CO-OPS id is
/// expected does not compile, and a constructed id is always valid. Every id
/// is also a StationKey.
template <Provider P>
class StationId {
 public:
  /// Trims ASCII whitespace, canonicalizes (P::canonical), then validates.
  [[nodiscard]] static constexpr std::expected<StationId, StationIdError> make(
      std::string_view raw) {
    const std::string_view trimmed = detail::trim(raw);
    if (trimmed.empty()) {
      return std::unexpected{StationIdError::empty};
    }
    std::string id = P::canonical(trimmed);
    if (not P::valid_id(id)) {
      return std::unexpected{StationIdError::invalid};
    }
    auto key = StationKey::make(std::move(id));
    if (not key) {
      return std::unexpected{StationIdError::invalid};
    }
    return StationId{*std::move(key)};
  }

  [[nodiscard]] constexpr std::string_view value() const& noexcept {
    return key_.view();
  }
  std::string_view value() const&& = delete;
  [[nodiscard]] constexpr const StationKey& key() const& noexcept {
    return key_;
  }
  const StationKey& key() const&& = delete;

  /// Ordered by the id text: map keys and sorted lists.
  friend constexpr auto operator<=>(const StationId&,
                                    const StationId&) = default;

 private:
  explicit constexpr StationId(StationKey k) : key_{std::move(k)} {}

  StationKey key_;
};

/// A provider's station as its station list describes it.
template <Provider P>
struct GaugeStation {
  StationId<P> id;
  StationText name;
  Location location;
  ValidRange validity;
  DatumTable datums;
  friend bool operator==(const GaugeStation&, const GaugeStation&) = default;
};

/// A station as files store it (C5, C14): its id and name, a WGS84 location
/// projected at the read boundary, and the file's own point when its CRS is
/// not WGS84. The name may be empty (the SN writer substitutes
/// "Station <id>").
struct FileStation {
  StationKey id;
  StationText name;
  Location location;
  std::optional<NativePoint> native;
  std::optional<DataSource> source;
  friend bool operator==(const FileStation&, const FileStation&) = default;
};

template <Provider P>
[[nodiscard]] FileStation to_file_station(const GaugeStation<P>& g) {
  return {.id = g.id.key(),
          .name = g.name,
          .location = g.location,
          .native = std::nullopt,
          .source = P::source};
}

}  // namespace mov::core
