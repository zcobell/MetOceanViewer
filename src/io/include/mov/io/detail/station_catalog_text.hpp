// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// What the station catalog's reader and writer share
// (docs/station-catalog.md): the token of every enumerator a file names, the
// member names, and the rounding that makes a value what its text says.
// Private to mov::io: public only because tests include it.

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <tuple>
#include <variant>

#include "mov/core/datum.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/provider/capabilities.hpp"
#include "mov/core/provider/coops.hpp"
#include "mov/core/station.hpp"
#include "mov/core/units.hpp"
#include "mov/io/detail/json_write.hpp"

namespace mov::io::detail::catalog {

// ---- the providers a catalog can hold
// ---------------------------------------

/// Which provider's catalog or station: one alternative per CatalogProvider,
/// in the order of StationCatalog. Dispatch is by visit, never by index.
using CatalogKind = std::variant<core::provider::Coops, core::provider::Usgs,
                                 core::provider::Ndbc>;

/// The member name of a provider's station block, the one table of them.
template <class P>
struct BlockName {
  std::string_view name;
};
inline constexpr std::tuple block_names{
    BlockName<core::provider::Coops>{.name = "coops"},
    BlockName<core::provider::Usgs>{.name = "usgs"},
    BlockName<core::provider::Ndbc>{.name = "ndbc"}};

template <class P>
[[nodiscard]] constexpr std::string_view block_name() noexcept {
  return std::get<BlockName<P>>(block_names).name;
}

[[nodiscard]] constexpr std::string_view block_name(CatalogKind kind) noexcept {
  return std::visit([]<class P>(P /*provider*/) { return block_name<P>(); },
                    kind);
}

[[nodiscard]] constexpr core::DataSource source_of(CatalogKind kind) noexcept {
  return std::visit([]<class P>(P /*provider*/) { return P::source; }, kind);
}

/// Every kind, in CatalogKind's order.
inline constexpr std::array<CatalogKind, 3> every_kind{
    CatalogKind{core::provider::Coops{}}, CatalogKind{core::provider::Usgs{}},
    CatalogKind{core::provider::Ndbc{}}};

/// The kind whose provider is `source`, or nullopt (a source with no
/// catalog: harmonics, model output, user files).
[[nodiscard]] constexpr std::optional<CatalogKind> kind_of(
    core::DataSource source) noexcept {
  const auto kind = std::ranges::find(
      every_kind, source, [](CatalogKind k) { return source_of(k); });
  if (kind == every_kind.end()) {
    return std::nullopt;
  }
  return *kind;
}

// ---- values
// -------------------------------------------------------------------

/// Decimals of what a file holds: positions in degrees, heights in metres.
inline constexpr int position_decimals = 6;
inline constexpr int height_decimals = 4;

template <class E>
struct TokenRow {
  E value;
  std::string_view token;
};

/// One row per enumerator, in enumerator order (checked below): the order
/// a writer prints a set in.
inline constexpr std::array observed_tokens{
    TokenRow<core::coops::Observed>{.value = core::coops::Observed::six_minute,
                                    .token = "six_minute"},
    TokenRow<core::coops::Observed>{.value = core::coops::Observed::one_minute,
                                    .token = "one_minute"},
    TokenRow<core::coops::Observed>{.value = core::coops::Observed::hourly,
                                    .token = "hourly"},
};
inline constexpr std::array met_tokens{
    TokenRow<core::coops::Met>{.value = core::coops::Met::air_temperature,
                               .token = "air_temperature"},
    TokenRow<core::coops::Met>{.value = core::coops::Met::water_temperature,
                               .token = "water_temperature"},
    TokenRow<core::coops::Met>{.value = core::coops::Met::wind,
                               .token = "wind"},
    TokenRow<core::coops::Met>{.value = core::coops::Met::air_pressure,
                               .token = "air_pressure"},
    TokenRow<core::coops::Met>{.value = core::coops::Met::humidity,
                               .token = "humidity"},
    TokenRow<core::coops::Met>{.value = core::coops::Met::conductivity,
                               .token = "conductivity"},
    TokenRow<core::coops::Met>{.value = core::coops::Met::salinity,
                               .token = "salinity"},
    TokenRow<core::coops::Met>{.value = core::coops::Met::visibility,
                               .token = "visibility"},
};
inline constexpr std::array predictions_tokens{
    TokenRow<core::coops::Predictions>{.value = core::coops::Predictions::none,
                                       .token = "none"},
    TokenRow<core::coops::Predictions>{
        .value = core::coops::Predictions::hilo_only, .token = "hilo_only"},
    TokenRow<core::coops::Predictions>{
        .value = core::coops::Predictions::any_interval,
        .token = "any_interval"},
};

template <class E, std::size_t N>
[[nodiscard]] constexpr bool in_enumerator_order(
    const std::array<TokenRow<E>, N>& rows) noexcept {
  for (std::size_t i = 0; i < N; ++i) {
    if (static_cast<std::size_t>(rows[i].value) != i) {
      return false;
    }
  }
  return true;
}
static_assert(in_enumerator_order(observed_tokens) and
              observed_tokens.size() == core::coops::every_observed.size());
static_assert(in_enumerator_order(met_tokens) and
              met_tokens.size() == core::coops::every_met.size());
static_assert(
    in_enumerator_order(predictions_tokens) and
    predictions_tokens.size() ==
        static_cast<std::size_t>(core::coops::Predictions::any_interval) + 1);

/// The token of an enumerator (its row).
template <class E, std::size_t N>
[[nodiscard]] constexpr std::string_view token_of(
    const std::array<TokenRow<E>, N>& rows, E value) noexcept {
  return rows[static_cast<std::size_t>(value)].token;
}

/// The enumerator of exactly this token, or nullopt.
template <class E, std::size_t N>
[[nodiscard]] constexpr std::optional<E> value_of(
    const std::array<TokenRow<E>, N>& rows, std::string_view token) noexcept {
  const auto row = std::ranges::find(rows, token, &TokenRow<E>::token);
  if (row == rows.end()) {
    return std::nullopt;
  }
  return row->value;
}

/// A CO-OPS datum's token: the core token ("NAVD88", "IGLD85", "STND").
[[nodiscard]] constexpr std::string_view datum_token(
    core::coops::Datum d) noexcept {
  return core::to_string(core::coops::to_vertical(d));
}

/// The CO-OPS datum of exactly this core token, or nullopt (NGVD29, HAT and
/// the other core datums the data API does not take are not CO-OPS datums).
[[nodiscard]] constexpr std::optional<core::coops::Datum> coops_datum_of(
    std::string_view token) noexcept {
  const auto d =
      std::ranges::find(core::coops::every_datum, token,
                        [](core::coops::Datum x) { return datum_token(x); });
  if (d == core::coops::every_datum.end()) {
    return std::nullopt;
  }
  return *d;
}

/// The core datum of exactly this token (no alias, no case folding).
[[nodiscard]] constexpr std::optional<core::VerticalDatum> vertical_datum_of(
    std::string_view token) noexcept {
  const auto& all = core::Enumerators<core::VerticalDatum>::values;
  const auto d = std::ranges::find(
      all, token, [](core::VerticalDatum x) { return core::to_string(x); });
  if (d == all.end()) {
    return std::nullopt;
  }
  return *d;
}

/// The position a file holds for `location`: each coordinate rounded to
/// position_decimals, normalized again (a longitude rounded to -180 is 180).
[[nodiscard]] inline core::Location canonical(core::Location location) {
  const auto rounded = core::Location::make(
      {.lat = json_text::rounded(location.lat(), position_decimals),
       .lon = json_text::rounded(location.lon(), position_decimals)});
  // Rounding a valid position keeps it within the ranges.
  return rounded.value_or(location);
}

/// A height as a file holds it: metres rounded to height_decimals.
[[nodiscard]] inline core::Length canonical(core::Length height) {
  return core::Length::in(
      json_text::rounded(height.as(core::LengthUnit::meter), height_decimals),
      core::LengthUnit::meter);
}

}  // namespace mov::io::detail::catalog
