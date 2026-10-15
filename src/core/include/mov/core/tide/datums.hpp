// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/detail/numeric.hpp"
#include "mov/core/units.hpp"

// The datums of a harmonics station (docs/harmonics-json.md 8.4-8.5,
// docs/harmonics-engine.md 2.5 and E6): heights above the station's MSL,
// each published by the source or computed from the harmonics.

namespace mov::core::tide {

/// The datum tokens of the format (docs/harmonics-json.md 8.4), in the
/// schema's order. MSL is not one: heights are above MSL, which is 0.
enum class DatumToken : std::uint8_t {
  hat,
  mhhw,
  mhw,
  dtl,
  mtl,
  mlw,
  mllw,
  lat,
  navd88,
  ngvd29,
  igld85,
  stnd
};

inline constexpr std::size_t datum_token_count = 12;

/// Every token, in enumerator order (the schema's `datum_token` enum, which
/// a test compares with it).
inline constexpr std::array<DatumToken, datum_token_count> all_datum_tokens{
    DatumToken::hat,    DatumToken::mhhw,   DatumToken::mhw,
    DatumToken::dtl,    DatumToken::mtl,    DatumToken::mlw,
    DatumToken::mllw,   DatumToken::lat,    DatumToken::navd88,
    DatumToken::ngvd29, DatumToken::igld85, DatumToken::stnd};
static_assert(static_cast<std::size_t>(DatumToken::stnd) + 1 ==
              datum_token_count);

/// The tokens computed_datums may hold (docs/harmonics-json.md 8.5): the
/// tidal datums, not the geodetic ones or the station datum.
inline constexpr std::array<DatumToken, 8> computable_datum_tokens{
    DatumToken::hat, DatumToken::mhhw, DatumToken::mhw,  DatumToken::dtl,
    DatumToken::mtl, DatumToken::mlw,  DatumToken::mllw, DatumToken::lat};

/// The file token: "HAT", "MLLW", "NAVD88". A switch without default, each
/// enumerator beside its token.
[[nodiscard]] constexpr std::string_view to_token(DatumToken d) noexcept {
  switch (d) {
    case DatumToken::hat:
      return "HAT";
    case DatumToken::mhhw:
      return "MHHW";
    case DatumToken::mhw:
      return "MHW";
    case DatumToken::dtl:
      return "DTL";
    case DatumToken::mtl:
      return "MTL";
    case DatumToken::mlw:
      return "MLW";
    case DatumToken::mllw:
      return "MLLW";
    case DatumToken::lat:
      return "LAT";
    case DatumToken::navd88:
      return "NAVD88";
    case DatumToken::ngvd29:
      return "NGVD29";
    case DatumToken::igld85:
      return "IGLD85";
    case DatumToken::stnd:
      return "STND";
  }
  return "STND";  // unreachable: every enumerator is handled
}

/// The token spelled exactly `token` (the file is case-sensitive).
[[nodiscard]] constexpr std::optional<DatumToken> parse_datum_token(
    std::string_view token) noexcept {
  for (const DatumToken d : all_datum_tokens) {
    if (to_token(d) == token) {
      return d;
    }
  }
  return std::nullopt;
}

/// Whether computed_datums may hold `d`.
[[nodiscard]] constexpr bool is_computable(DatumToken d) noexcept {
  for (const DatumToken c : computable_datum_tokens) {
    if (c == d) {
      return true;
    }
  }
  return false;
}

/// The station's mean sea level: the reference of every height in the file.
struct MeanSeaLevel {
  friend constexpr bool operator==(MeanSeaLevel, MeanSeaLevel) = default;
};

/// A datum a subordinate's offsets can be relative to: MSL or a token
/// (docs/harmonics-json.md 8.3, `offsets.datum`).
using TideDatum = std::variant<MeanSeaLevel, DatumToken>;

/// "MSL" or the token.
[[nodiscard]] constexpr std::string_view to_token(const TideDatum& d) noexcept {
  return std::holds_alternative<MeanSeaLevel>(d)
             ? std::string_view{"MSL"}
             : to_token(std::get<DatumToken>(d));
}

/// "MSL" or a datum token, exactly.
[[nodiscard]] constexpr std::optional<TideDatum> parse_tide_datum(
    std::string_view token) noexcept {
  if (token == "MSL") {
    return TideDatum{MeanSeaLevel{}};
  }
  if (const auto d = parse_datum_token(token)) {
    return TideDatum{*d};
  }
  return std::nullopt;
}

/// The core datum of `d`, where core's VerticalDatum has one. HAT, LAT and
/// DTL have none yet (docs/harmonics-engine.md 2.6): their heights are kept
/// and reported by W-DATUM-UNSUPPORTED, but a DatumTable cannot shift to
/// them.
[[nodiscard]] constexpr std::optional<VerticalDatum> to_vertical_datum(
    DatumToken d) noexcept {
  switch (d) {
    case DatumToken::mhhw:
      return VerticalDatum::mhhw;
    case DatumToken::mhw:
      return VerticalDatum::mhw;
    case DatumToken::mtl:
      return VerticalDatum::mtl;
    case DatumToken::mlw:
      return VerticalDatum::mlw;
    case DatumToken::mllw:
      return VerticalDatum::mllw;
    case DatumToken::navd88:
      return VerticalDatum::navd88;
    case DatumToken::ngvd29:
      return VerticalDatum::ngvd29;
    case DatumToken::igld85:
      return VerticalDatum::igld85;
    case DatumToken::stnd:
      return VerticalDatum::stnd;
    case DatumToken::hat:
    case DatumToken::dtl:
    case DatumToken::lat:
      return std::nullopt;
  }
  return std::nullopt;  // unreachable: every enumerator is handled
}

[[nodiscard]] constexpr std::optional<VerticalDatum> to_vertical_datum(
    const TideDatum& d) noexcept {
  return std::holds_alternative<MeanSeaLevel>(d)
             ? std::optional<VerticalDatum>{VerticalDatum::msl}
             : to_vertical_datum(std::get<DatumToken>(d));
}

/// One member of a `datums` or `computed_datums.heights` object.
struct TokenHeight {
  DatumToken datum;
  Length height;
  friend constexpr bool operator==(const TokenHeight&,
                                   const TokenHeight&) = default;
};

enum class DatumHeightsErrc : std::uint8_t {
  duplicate,  // the token appears twice
  not_finite  // the height is NaN or infinite
};
struct DatumHeightsError {
  DatumToken datum;
  DatumHeightsErrc code;
  friend constexpr bool operator==(const DatumHeightsError&,
                                   const DatumHeightsError&) = default;
};

/// Token -> height above MSL, each token at most once, every height finite.
/// The default is empty.
class DatumHeights {
 public:
  constexpr DatumHeights() noexcept = default;

  /// The first duplicate or non-finite row, in input order, is the error.
  [[nodiscard]] static constexpr std::expected<DatumHeights, DatumHeightsError>
  make(std::span<const TokenHeight> rows) noexcept {
    std::array<std::optional<Length>, datum_token_count> heights{};
    for (const TokenHeight& row : rows) {
      if (not detail::is_finite(row.height.as(LengthUnit::meter))) {
        return std::unexpected(DatumHeightsError{
            .datum = row.datum, .code = DatumHeightsErrc::not_finite});
      }
      std::optional<Length>& slot = heights[index(row.datum)];
      if (slot) {
        return std::unexpected(DatumHeightsError{
            .datum = row.datum, .code = DatumHeightsErrc::duplicate});
      }
      slot = row.height;
    }
    return DatumHeights{heights};
  }

  [[nodiscard]] constexpr std::optional<Length> height(
      DatumToken d) const noexcept {
    return heights_[index(d)];
  }

  [[nodiscard]] constexpr bool empty() const noexcept {
    for (const auto& h : heights_) {
      if (h) {
        return false;
      }
    }
    return true;
  }

  /// The rows, in token order.
  [[nodiscard]] constexpr std::vector<TokenHeight> rows() const {
    std::vector<TokenHeight> out;
    for (const DatumToken d : all_datum_tokens) {
      if (const auto h = height(d)) {
        out.push_back(TokenHeight{.datum = d, .height = *h});
      }
    }
    return out;
  }

  friend constexpr bool operator==(const DatumHeights&,
                                   const DatumHeights&) = default;

 private:
  explicit constexpr DatumHeights(
      const std::array<std::optional<Length>, datum_token_count>&
          heights) noexcept
      : heights_{heights} {}

  [[nodiscard]] static constexpr std::size_t index(DatumToken d) noexcept {
    return static_cast<std::size_t>(d);
  }

  std::array<std::optional<Length>, datum_token_count> heights_{};
};

/// The period [start, end) over which computed datums were computed: at
/// least one nodal cycle of 18.61 Julian years (docs/harmonics-json.md 8.5).
struct DatumPeriod {
  std::chrono::sys_days start;
  std::chrono::sys_days end;
  friend constexpr bool operator==(const DatumPeriod&,
                                   const DatumPeriod&) = default;
};

/// 18.61 Julian years is 6,797.3025 days: a period is long enough when
/// days x 10,000 >= 67,973,025.
[[nodiscard]] constexpr bool spans_nodal_cycle(
    std::chrono::sys_days start, std::chrono::sys_days end) noexcept {
  return (end - start).count() * std::int64_t{10'000} >=
         std::int64_t{67'973'025};
}

/// Datums computed from the harmonics, not observed (docs/harmonics-json.md
/// 8.5).
struct ComputedDatums {
  DatumPeriod period;
  DatumHeights heights;
  friend constexpr bool operator==(const ComputedDatums&,
                                   const ComputedDatums&) = default;
};

/// Whether a height was published by the source or computed from the
/// harmonics ("computed from harmonics, not observed" wherever it is shown).
enum class DatumOrigin : std::uint8_t { published, computed };

/// A token is both published and computed (E-DATUM-CONFLICT).
struct BothOrigins {
  DatumToken datum;
  friend constexpr bool operator==(BothOrigins, BothOrigins) = default;
};
/// A computed token outside the tidal datums (computed_datum_token).
struct NotComputable {
  DatumToken datum;
  friend constexpr bool operator==(NotComputable, NotComputable) = default;
};
/// The computed period spans less than 18.61 years (E-DATUM-CONFLICT).
struct ShortPeriod {
  friend constexpr bool operator==(ShortPeriod, ShortPeriod) = default;
};
using DatumsError = std::variant<NotComputable, BothOrigins, ShortPeriod>;

/// What a station knows about its datums: published heights, computed
/// heights with their period, and the epoch of the published ones. A token
/// is in at most one of the two (a published value is never shadowed by a
/// computed one). The default knows only MSL.
class Datums {
 public:
  constexpr Datums() noexcept = default;

  /// Checks, in order: the computed tokens are tidal datums, each in token
  /// order; no token is in both, in token order; the period spans a nodal
  /// cycle.
  [[nodiscard]] static constexpr std::expected<Datums, DatumsError> make(
      DatumHeights published, std::optional<ComputedDatums> computed,
      std::optional<std::string> published_epoch) {
    if (computed) {
      for (const DatumToken d : all_datum_tokens) {
        if (computed->heights.height(d) and not is_computable(d)) {
          return std::unexpected(DatumsError{NotComputable{.datum = d}});
        }
      }
      for (const DatumToken d : all_datum_tokens) {
        if (published.height(d) and computed->heights.height(d)) {
          return std::unexpected(DatumsError{BothOrigins{.datum = d}});
        }
      }
      if (not spans_nodal_cycle(computed->period.start, computed->period.end)) {
        return std::unexpected(DatumsError{ShortPeriod{}});
      }
    }
    return Datums{published, computed, std::move(published_epoch)};
  }

  /// The height of `d` above MSL, from either set; MSL is 0.
  [[nodiscard]] constexpr std::optional<Length> height(
      const TideDatum& d) const noexcept {
    if (std::holds_alternative<MeanSeaLevel>(d)) {
      return Length{};
    }
    const DatumToken token = std::get<DatumToken>(d);
    if (const auto h = published_.height(token)) {
      return h;
    }
    return computed_ ? computed_->heights.height(token) : std::nullopt;
  }

  /// Published or computed; nullopt when the station does not know `d`.
  /// MSL counts as published (0 by definition).
  [[nodiscard]] constexpr std::optional<DatumOrigin> origin(
      const TideDatum& d) const noexcept {
    if (std::holds_alternative<MeanSeaLevel>(d) or
        published_.height(std::get<DatumToken>(d))) {
      return DatumOrigin::published;
    }
    if (computed_ and computed_->heights.height(std::get<DatumToken>(d))) {
      return DatumOrigin::computed;
    }
    return std::nullopt;
  }

  [[nodiscard]] constexpr const DatumHeights& published() const& noexcept {
    return published_;
  }
  const DatumHeights& published() const&& = delete;
  [[nodiscard]] constexpr const std::optional<ComputedDatums>& computed()
      const& noexcept {
    return computed_;
  }
  const std::optional<ComputedDatums>& computed() const&& = delete;
  [[nodiscard]] constexpr const std::optional<std::string>& published_epoch()
      const& noexcept {
    return published_epoch_;
  }
  const std::optional<std::string>& published_epoch() const&& = delete;

  /// Both sets as a core DatumTable, for shift (CD 2.11): every token that
  /// has a VerticalDatum (not HAT, LAT, DTL yet).
  [[nodiscard]] constexpr DatumTable table() const noexcept {
    std::array<DatumHeight, datum_token_count> rows{};
    std::size_t count = 0;
    for (const DatumToken d : all_datum_tokens) {
      const auto core = to_vertical_datum(d);
      const auto h = height(TideDatum{d});
      if (core and h) {
        rows[count] = DatumHeight{.datum = *core, .height = *h};
        ++count;
      }
    }
    // Finite heights of distinct datums above MSL: from_heights cannot fail.
    const auto table = DatumTable::from_heights(VerticalDatum::msl,
                                                std::span{rows}.first(count));
    return table ? *table : DatumTable{};
  }

  friend constexpr bool operator==(const Datums&, const Datums&) = default;

 private:
  constexpr Datums(DatumHeights published,
                   std::optional<ComputedDatums> computed,
                   std::optional<std::string> published_epoch) noexcept
      : published_{published},
        computed_{computed},
        published_epoch_{std::move(published_epoch)} {}

  DatumHeights published_;
  std::optional<ComputedDatums> computed_;
  std::optional<std::string> published_epoch_;
};

}  // namespace mov::core::tide
