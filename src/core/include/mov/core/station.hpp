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
#include <string>
#include <string_view>
#include <utility>

#include "mov/core/datum.hpp"
#include "mov/core/detail/ascii.hpp"
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

// Indexed by the enumerator.
inline constexpr std::array<std::string_view, 7> data_source_tokens{
    "noaa_coops", "usgs", "ndbc", "xtide", "adcirc", "dflowfm", "user"};
static_assert(data_source_tokens.size() ==
              static_cast<std::size_t>(DataSource::user) + 1);

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
  return detail::data_source_tokens[static_cast<std::size_t>(s)];
}

/// The source with exactly this token (case-sensitive), or nullopt.
[[nodiscard]] constexpr std::optional<DataSource> parse_data_source(
    std::string_view token) noexcept {
  const auto it = std::ranges::find(detail::data_source_tokens, token);
  if (it == detail::data_source_tokens.end()) {
    return std::nullopt;
  }
  return static_cast<DataSource>(it - detail::data_source_tokens.begin());
}

/// The station providers. Each names its DataSource, whether its ids are
/// upper-cased before validation, and the predicate a (trimmed, folded) id
/// must satisfy. StationId<P>::make is the only way to an id.
namespace provider {

/// NOAA CO-OPS: ^[0-9]{7}$
struct Coops {
  static constexpr DataSource source = DataSource::noaa_coops;
  static constexpr bool upper_case_ids = false;
  [[nodiscard]] static constexpr bool valid_id(std::string_view id) noexcept {
    return id.size() == 7 and std::ranges::all_of(id, detail::is_digit);
  }
};

/// USGS Water Data monitoring location: ^[A-Za-z0-9]+-[A-Za-z0-9]+$
/// ("USGS-07374000").
struct Usgs {
  static constexpr DataSource source = DataSource::usgs;
  static constexpr bool upper_case_ids = false;
  [[nodiscard]] static constexpr bool valid_id(std::string_view id) noexcept {
    const auto dash = id.find('-');
    if (dash == std::string_view::npos) {
      return false;
    }
    const std::string_view agency = id.substr(0, dash);
    const std::string_view number = id.substr(dash + 1);
    return not agency.empty() and not number.empty() and
           std::ranges::all_of(agency, detail::is_alnum) and
           std::ranges::all_of(number, detail::is_alnum);
  }
};

/// NDBC: ^[A-Z0-9]{5}$ after upper-casing.
struct Ndbc {
  static constexpr DataSource source = DataSource::ndbc;
  static constexpr bool upper_case_ids = true;
  [[nodiscard]] static constexpr bool valid_id(std::string_view id) noexcept {
    return id.size() == 5 and std::ranges::all_of(id, detail::is_upper_alnum);
  }
};

/// XTide: the station name. Non-empty, at most 255 bytes, no control
/// characters (bytes below 0x20 and 0x7F); other bytes, UTF-8 included, pass.
struct Xtide {
  static constexpr DataSource source = DataSource::xtide;
  static constexpr bool upper_case_ids = false;
  static constexpr std::size_t max_id_bytes = 255;
  [[nodiscard]] static constexpr bool valid_id(std::string_view id) noexcept {
    return not id.empty() and id.size() <= max_id_bytes and
           std::ranges::none_of(id, detail::is_control);
  }
};

}  // namespace provider

template <class P>
concept Provider = requires(std::string_view s) {
  { P::source } -> std::convertible_to<DataSource>;
  { P::upper_case_ids } -> std::convertible_to<bool>;
  { P::valid_id(s) } -> std::same_as<bool>;
};

enum class StationIdError : std::uint8_t {
  empty,    // nothing but whitespace
  invalid,  // fails the provider's id grammar
};

/// A station id of one provider. Passing a USGS id where a CO-OPS id is
/// expected does not compile, and a constructed id is always valid.
template <Provider P>
class StationId {
 public:
  /// Trims ASCII whitespace, upper-cases if the provider asks for it, then
  /// validates.
  [[nodiscard]] static constexpr std::expected<StationId, StationIdError> make(
      std::string_view raw) {
    const std::string_view trimmed = detail::trim(raw);
    if (trimmed.empty()) {
      return std::unexpected{StationIdError::empty};
    }
    std::string id{trimmed};
    if constexpr (P::upper_case_ids) {
      std::ranges::transform(id, id.begin(), detail::to_upper);
    }
    if (not P::valid_id(id)) {
      return std::unexpected{StationIdError::invalid};
    }
    return StationId{std::move(id)};
  }

  [[nodiscard]] constexpr std::string_view value() const& noexcept {
    return value_;
  }
  std::string_view value() const&& = delete;

  /// Ordered by the id text: map keys and sorted lists.
  friend constexpr auto operator<=>(const StationId&,
                                    const StationId&) = default;

 private:
  explicit constexpr StationId(std::string v) : value_{std::move(v)} {}

  std::string value_;
};

/// A provider's station as its station list describes it.
template <Provider P>
struct GaugeStation {
  StationId<P> id;
  std::string name;
  Location location;
  ValidRange validity;
  DatumTable datums;
  friend bool operator==(const GaugeStation&, const GaugeStation&) = default;
};

/// A station as files store it (C5, C14): any id text (StationTable checks
/// it), a WGS84 location projected at the read boundary, and the file's own
/// point when its CRS is not WGS84.
struct FileStation {
  std::string id;
  std::string name;
  Location location;
  std::optional<NativePoint> native;
  std::optional<DataSource> source;
  friend bool operator==(const FileStation&, const FileStation&) = default;
};

template <Provider P>
[[nodiscard]] FileStation to_file_station(const GaugeStation<P>& g) {
  return {.id = std::string{g.id.value()},
          .name = g.name,
          .location = g.location,
          .native = std::nullopt,
          .source = P::source};
}

}  // namespace mov::core
