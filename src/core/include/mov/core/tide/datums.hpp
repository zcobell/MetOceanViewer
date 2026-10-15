// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/detail/numeric.hpp"
#include "mov/core/enum_set.hpp"
#include "mov/core/units.hpp"

// The datums of a harmonics station (docs/harmonics-json.md 8.4-8.5,
// docs/harmonics-engine.md 2.7 and E6): heights above the station's MSL,
// each published by the source or computed from the harmonics. The file's
// datum tokens are core's VerticalDatum tokens.

namespace mov::core::tide {

/// The datum spelled exactly `token`, as the file spells it (upper case;
/// parse_vertical_datum, which folds case and knows aliases, is for other
/// sources). "MSL" is one: `offsets.datum` may name it.
[[nodiscard]] constexpr std::optional<VerticalDatum> parse_file_datum(
    std::string_view token) noexcept {
  for (const VerticalDatum d : detail::all_datums) {
    if (to_string(d) == token) {
      return d;
    }
  }
  return std::nullopt;
}

/// The datums computed_datums may hold (docs/harmonics-json.md 8.5): the
/// tidal ones, not the geodetic ones or the station datum.
enum class ComputableDatum : std::uint8_t {
  hat,
  mhhw,
  mhw,
  dtl,
  mtl,
  mlw,
  mllw,
  lat
};

/// Every computable datum, in enumerator order.
inline constexpr std::array<ComputableDatum, 8> all_computable_datums{
    ComputableDatum::hat,  ComputableDatum::mhhw, ComputableDatum::mhw,
    ComputableDatum::dtl,  ComputableDatum::mtl,  ComputableDatum::mlw,
    ComputableDatum::mllw, ComputableDatum::lat};
static_assert(static_cast<std::size_t>(ComputableDatum::lat) + 1 ==
              all_computable_datums.size());

/// The core datum. A switch without default, each enumerator beside its
/// datum.
[[nodiscard]] constexpr VerticalDatum vertical_datum(
    ComputableDatum d) noexcept {
  switch (d) {
    case ComputableDatum::hat:
      return VerticalDatum::hat;
    case ComputableDatum::mhhw:
      return VerticalDatum::mhhw;
    case ComputableDatum::mhw:
      return VerticalDatum::mhw;
    case ComputableDatum::dtl:
      return VerticalDatum::dtl;
    case ComputableDatum::mtl:
      return VerticalDatum::mtl;
    case ComputableDatum::mlw:
      return VerticalDatum::mlw;
    case ComputableDatum::mllw:
      return VerticalDatum::mllw;
    case ComputableDatum::lat:
      return VerticalDatum::lat;
  }
  return VerticalDatum::lat;  // unreachable: every enumerator is handled
}

/// The computable datum that is `d`, if `d` is one.
[[nodiscard]] constexpr std::optional<ComputableDatum> computable(
    VerticalDatum d) noexcept {
  for (const ComputableDatum c : all_computable_datums) {
    if (vertical_datum(c) == d) {
      return c;
    }
  }
  return std::nullopt;
}

/// The file token: "HAT", "MLLW".
[[nodiscard]] constexpr std::string_view to_string(ComputableDatum d) noexcept {
  return mov::core::to_string(vertical_datum(d));
}

}  // namespace mov::core::tide

namespace mov::core::detail {

/// The keys a Heights table holds: every datum but MSL (heights are above
/// MSL), or every computable datum.
template <class Key>
struct HeightKeys;

template <>
struct HeightKeys<VerticalDatum> {
  static constexpr const auto& all = all_datums;
  [[nodiscard]] static constexpr bool admissible(VerticalDatum d) noexcept {
    return d != VerticalDatum::msl;
  }
};

template <>
struct HeightKeys<tide::ComputableDatum> {
  static constexpr const auto& all = tide::all_computable_datums;
  [[nodiscard]] static constexpr bool admissible(
      tide::ComputableDatum /*d*/) noexcept {
    return true;
  }
};

}  // namespace mov::core::detail

namespace mov::core::tide {

/// One member of a `datums` or `computed_datums.heights` object.
template <class Key>
struct HeightRow {
  Key datum;
  Length height;
  friend constexpr bool operator==(const HeightRow&,
                                   const HeightRow&) = default;
};

enum class HeightsErrc : std::uint8_t {
  msl,        // MSL listed: it is 0 by definition and never listed
  duplicate,  // the datum appears twice
  not_finite  // the height is NaN or infinite
};

template <class Key>
struct HeightsError {
  Key datum;
  HeightsErrc code;
  friend constexpr bool operator==(const HeightsError&,
                                   const HeightsError&) = default;
};

/// Datum -> height above MSL, each datum at most once, every height finite,
/// never MSL itself. The default is empty. Regular and trivially copyable.
template <class Key>
class Heights {
 public:
  using Row = HeightRow<Key>;
  using Error = HeightsError<Key>;

  constexpr Heights() noexcept = default;

  /// The first refused row, in input order, is the error.
  [[nodiscard]] static constexpr std::expected<Heights, Error> make(
      std::span<const Row> rows) noexcept {
    Slots slots{};
    for (const Row& row : rows) {
      if (not Keys::admissible(row.datum)) {
        return std::unexpected(
            Error{.datum = row.datum, .code = HeightsErrc::msl});
      }
      if (not detail::is_finite(row.height.as(LengthUnit::meter))) {
        return std::unexpected(
            Error{.datum = row.datum, .code = HeightsErrc::not_finite});
      }
      std::optional<Length>& slot = slots[index(row.datum)];
      if (slot) {
        return std::unexpected(
            Error{.datum = row.datum, .code = HeightsErrc::duplicate});
      }
      slot = row.height;
    }
    return Heights{slots};
  }

  [[nodiscard]] constexpr std::optional<Length> height(Key d) const noexcept {
    return slots_[index(d)];
  }

  [[nodiscard]] constexpr bool empty() const noexcept {
    return std::ranges::none_of(
        slots_, [](const std::optional<Length>& h) { return h.has_value(); });
  }

  /// The rows, in enumerator order.
  [[nodiscard]] constexpr std::vector<Row> rows() const {
    std::vector<Row> out;
    for (const Key d : Keys::all) {
      if (const auto h = height(d)) {
        out.push_back(Row{.datum = d, .height = *h});
      }
    }
    return out;
  }

  friend constexpr bool operator==(const Heights&, const Heights&) = default;

 private:
  using Keys = detail::HeightKeys<Key>;
  using Slots = std::array<std::optional<Length>, Keys::all.size()>;

  explicit constexpr Heights(const Slots& slots) noexcept : slots_{slots} {}

  [[nodiscard]] static constexpr std::size_t index(Key d) noexcept {
    return static_cast<std::size_t>(d);
  }

  Slots slots_{};
};

/// Published heights (`datums`): every datum but MSL.
using DatumHeights = Heights<VerticalDatum>;
/// Computed heights (`computed_datums.heights`): the tidal datums only, so a
/// computed geodetic datum cannot be expressed.
using ComputedHeights = Heights<ComputableDatum>;
static_assert(std::is_trivially_copyable_v<DatumHeights>);
static_assert(std::is_trivially_copyable_v<ComputedHeights>);

enum class DatumPeriodError : std::uint8_t {
  inverted,  // end before start (E-DATE)
  too_short  // less than 18.61 Julian years, or empty (E-DATUM-CONFLICT)
};

/// The period [start, end) over which computed datums were computed: at
/// least one nodal cycle of 18.61 Julian years (docs/harmonics-json.md 8.5),
/// enforced by make().
class DatumPeriod {
 public:
  /// 18.61 Julian years is 6,797.3025 days: long enough when
  /// days x 10,000 >= 67,973,025.
  [[nodiscard]] static constexpr std::expected<DatumPeriod, DatumPeriodError>
  make(std::chrono::sys_days start, std::chrono::sys_days end) noexcept {
    if (end < start) {
      return std::unexpected(DatumPeriodError::inverted);
    }
    if ((end - start).count() * std::int64_t{10'000} <
        std::int64_t{67'973'025}) {
      return std::unexpected(DatumPeriodError::too_short);
    }
    return DatumPeriod{start, end};
  }

  [[nodiscard]] constexpr std::chrono::sys_days start() const noexcept {
    return start_;
  }
  [[nodiscard]] constexpr std::chrono::sys_days end() const noexcept {
    return end_;
  }

  friend constexpr bool operator==(const DatumPeriod&,
                                   const DatumPeriod&) = default;

 private:
  constexpr DatumPeriod(std::chrono::sys_days start,
                        std::chrono::sys_days end) noexcept
      : start_{start}, end_{end} {}

  std::chrono::sys_days start_;
  std::chrono::sys_days end_;
};
static_assert(std::is_trivially_copyable_v<DatumPeriod>);

/// Datums computed from the harmonics, not observed (docs/harmonics-json.md
/// 8.5).
struct ComputedDatums {
  DatumPeriod period;
  ComputedHeights heights;
  friend constexpr bool operator==(const ComputedDatums&,
                                   const ComputedDatums&) = default;
};
static_assert(std::is_trivially_copyable_v<ComputedDatums>);

/// Whether a height was published by the source or computed from the
/// harmonics ("computed from harmonics, not observed" wherever it is shown).
enum class DatumOrigin : std::uint8_t { published, computed };

/// The datums that are both published and computed (E-DATUM-CONFLICT), all
/// of them.
struct BothOrigins {
  EnumSet<VerticalDatum> datums;
  friend constexpr bool operator==(const BothOrigins&,
                                   const BothOrigins&) = default;
};

/// What a station knows about its datums: published heights with their
/// epoch, and computed heights with their period. A datum is in at most one
/// of the two: a published value is never shadowed by a computed one. The
/// default knows only MSL.
class Datums {
 public:
  constexpr Datums() noexcept = default;

  /// Refuses every datum in both sets (BothOrigins lists them all).
  [[nodiscard]] static constexpr std::expected<Datums, BothOrigins> make(
      const DatumHeights& published, std::optional<ComputedDatums> computed,
      std::optional<std::string> published_epoch) {
    EnumSet<VerticalDatum> both;
    if (computed) {
      for (const ComputableDatum c : all_computable_datums) {
        if (computed->heights.height(c) and
            published.height(vertical_datum(c))) {
          both.insert(vertical_datum(c));
        }
      }
    }
    if (not both.empty()) {
      return std::unexpected(BothOrigins{.datums = both});
    }
    return Datums{published, computed, std::move(published_epoch)};
  }

  /// The height of `d` above MSL, from either set; MSL is 0.
  [[nodiscard]] constexpr std::optional<Length> height(
      VerticalDatum d) const noexcept {
    if (d == VerticalDatum::msl) {
      return Length{};
    }
    if (const auto h = published_.height(d)) {
      return h;
    }
    return computed_height(d);
  }

  /// Published or computed; nullopt when the station does not know `d`.
  /// MSL counts as published (0 by definition).
  [[nodiscard]] constexpr std::optional<DatumOrigin> origin(
      VerticalDatum d) const noexcept {
    if (d == VerticalDatum::msl or published_.height(d)) {
      return DatumOrigin::published;
    }
    if (computed_height(d)) {
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

  /// Both sets as a core DatumTable, for shift (CD 2.11).
  [[nodiscard]] constexpr DatumTable table() const noexcept {
    std::array<DatumHeight, detail::all_datums.size()> rows{};
    std::size_t count = 0;
    for (const VerticalDatum d : detail::all_datums) {
      if (const auto h = height(d); h and d != VerticalDatum::msl) {
        rows[count] = DatumHeight{.datum = d, .height = *h};
        ++count;
      }
    }
    // Finite heights of distinct datums, MSL not among them, above MSL:
    // from_heights has nothing to refuse.
    const auto table = DatumTable::from_heights(VerticalDatum::msl,
                                                std::span{rows}.first(count));
    assert(table.has_value());
    return *table;
  }

  friend constexpr bool operator==(const Datums&, const Datums&) = default;

 private:
  constexpr Datums(const DatumHeights& published,
                   std::optional<ComputedDatums> computed,
                   std::optional<std::string> published_epoch) noexcept
      : published_{published},
        computed_{computed},
        published_epoch_{std::move(published_epoch)} {}

  [[nodiscard]] constexpr std::optional<Length> computed_height(
      VerticalDatum d) const noexcept {
    const auto c = computable(d);
    if (not computed_ or not c) {
      return std::nullopt;
    }
    return computed_->heights.height(*c);
  }

  DatumHeights published_;
  std::optional<ComputedDatums> computed_;
  std::optional<std::string> published_epoch_;
};

}  // namespace mov::core::tide
