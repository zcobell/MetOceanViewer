// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <compare>
#include <concepts>
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

#include "mov/core/ascii.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/core/windows.hpp"

/// The request vocabulary of the USGS Water Data API
/// (docs/providers-design.md section 3.4). A continuous request has a time
/// range and the instantaneous statistic; a daily request has a range of
/// calendar days and names its statistic when it selects by parameter.
namespace mov::core::usgs {

enum class CodeError : std::uint8_t {
  wrong_length,
  invalid_character,
};

namespace detail {

[[nodiscard]] constexpr bool is_lower_hex(char c) noexcept {
  return ascii::is_digit(c) or (c >= 'a' and c <= 'f');
}

/// The N characters of `text` when each satisfies `valid`.
template <std::size_t N, class Valid>
[[nodiscard]] constexpr std::expected<std::array<char, N>, CodeError>
fixed_code(std::string_view text, Valid valid) noexcept {
  if (text.size() != N) {
    return std::unexpected{CodeError::wrong_length};
  }
  if (not std::ranges::all_of(text, valid)) {
    return std::unexpected{CodeError::invalid_character};
  }
  std::array<char, N> chars{};
  std::ranges::copy(text, chars.begin());
  return chars;
}

}  // namespace detail

/// A USGS parameter code: five ASCII digits ("00060" discharge, "00065" gage
/// height). Built only by make, which accepts exactly that (no trimming).
class ParameterCode {
 public:
  [[nodiscard]] static constexpr std::expected<ParameterCode, CodeError> make(
      std::string_view text) noexcept {
    const auto chars = detail::fixed_code<5>(text, ascii::is_digit);
    if (not chars) {
      return std::unexpected{chars.error()};
    }
    return ParameterCode{*chars};
  }

  [[nodiscard]] constexpr std::string_view view() const& noexcept {
    return {chars_.data(), chars_.size()};
  }
  std::string_view view() const&& = delete;

  friend constexpr auto operator<=>(const ParameterCode&,
                                    const ParameterCode&) = default;

 private:
  explicit constexpr ParameterCode(std::array<char, 5> chars) noexcept
      : chars_{chars} {}

  std::array<char, 5> chars_;
};

/// A USGS statistic code: five ASCII digits. A different type from
/// ParameterCode, so the two cannot be swapped.
class StatisticId {
 public:
  [[nodiscard]] static constexpr std::expected<StatisticId, CodeError> make(
      std::string_view text) noexcept {
    const auto chars = detail::fixed_code<5>(text, ascii::is_digit);
    if (not chars) {
      return std::unexpected{chars.error()};
    }
    return StatisticId{*chars};
  }

  /// 00011: instantaneous values, the statistic of every continuous series.
  [[nodiscard]] static constexpr StatisticId instantaneous() noexcept {
    return StatisticId{{'0', '0', '0', '1', '1'}};
  }
  /// 00003: the daily mean.
  [[nodiscard]] static constexpr StatisticId mean() noexcept {
    return StatisticId{{'0', '0', '0', '0', '3'}};
  }
  /// 00002: the daily minimum.
  [[nodiscard]] static constexpr StatisticId minimum() noexcept {
    return StatisticId{{'0', '0', '0', '0', '2'}};
  }
  /// 00001: the daily maximum.
  [[nodiscard]] static constexpr StatisticId maximum() noexcept {
    return StatisticId{{'0', '0', '0', '0', '1'}};
  }

  [[nodiscard]] constexpr std::string_view view() const& noexcept {
    return {chars_.data(), chars_.size()};
  }
  std::string_view view() const&& = delete;

  friend constexpr auto operator<=>(const StatisticId&,
                                    const StatisticId&) = default;

 private:
  explicit constexpr StatisticId(std::array<char, 5> chars) noexcept
      : chars_{chars} {}

  std::array<char, 5> chars_;
};

/// The id of one time series (`combined-metadata`'s `id`, the data
/// collections' `time_series_id`): 32 lower-case hexadecimal digits.
class TimeSeriesId {
 public:
  [[nodiscard]] static constexpr std::expected<TimeSeriesId, CodeError> make(
      std::string_view text) noexcept {
    const auto chars = detail::fixed_code<32>(text, detail::is_lower_hex);
    if (not chars) {
      return std::unexpected{chars.error()};
    }
    return TimeSeriesId{*chars};
  }

  [[nodiscard]] constexpr std::string_view view() const& noexcept {
    return {chars_.data(), chars_.size()};
  }
  std::string_view view() const&& = delete;

  friend constexpr auto operator<=>(const TimeSeriesId&,
                                    const TimeSeriesId&) = default;

 private:
  explicit constexpr TimeSeriesId(std::array<char, 32> chars) noexcept
      : chars_{chars} {}

  std::array<char, 32> chars_;
};

/// The site's Primary continuous series of a parameter (statistic 00011 by
/// construction of Continuous).
struct ByParameter {
  ParameterCode parameter;
  friend constexpr bool operator==(const ByParameter&,
                                   const ByParameter&) = default;
};
/// The site's Primary daily series of a parameter and statistic.
struct DailyByParameter {
  ParameterCode parameter;
  StatisticId statistic;
  friend constexpr bool operator==(const DailyByParameter&,
                                   const DailyByParameter&) = default;
};
/// One series by its id, whatever its parameter.
struct ByTimeSeries {
  TimeSeriesId id;
  friend constexpr bool operator==(const ByTimeSeries&,
                                   const ByTimeSeries&) = default;
};

/// Instantaneous values over a time range (the `continuous` collection).
struct Continuous {
  StationId<provider::Usgs> station;
  std::variant<ByParameter, ByTimeSeries> series;
  TimeRange range;
  friend constexpr bool operator==(const Continuous&,
                                   const Continuous&) = default;
};

/// Daily values over calendar days (the `daily` collection, decision 34).
struct Daily {
  StationId<provider::Usgs> station;
  std::variant<DailyByParameter, ByTimeSeries> series;
  DayRange days;
  friend constexpr bool operator==(const Daily&, const Daily&) = default;
};

using Request = std::variant<Continuous, Daily>;

/// The collection a statistic's series belong to: 00011 (instantaneous) is
/// the `continuous` collection, any other statistic the `daily` one.
enum class Collection : std::uint8_t { continuous, daily };

[[nodiscard]] constexpr Collection collection_of(
    const StatisticId& statistic) noexcept {
  return statistic == StatisticId::instantaneous() ? Collection::continuous
                                                   : Collection::daily;
}

/// One series of a site, as `combined-metadata` describes it: a row of the
/// station catalog (Capabilities<Usgs>), of the series picker, and the input
/// of resolution.
struct SeriesInfo {
  TimeSeriesId id;
  ParameterCode parameter;
  StatisticId statistic;
  std::string parameter_name;
  bool primary;
  std::optional<Unit> unit;
  ValidRange validity;
  friend bool operator==(const SeriesInfo&, const SeriesInfo&) = default;
};

/// Two rows with one id.
struct DuplicateSeriesId {
  TimeSeriesId id;
  friend constexpr bool operator==(const DuplicateSeriesId&,
                                   const DuplicateSeriesId&) = default;
};
/// Two rows are the Primary series of one parameter and statistic.
struct SeveralPrimary {
  ParameterCode parameter;
  StatisticId statistic;
  friend constexpr bool operator==(const SeveralPrimary&,
                                   const SeveralPrimary&) = default;
};
using SeriesSetError = std::variant<DuplicateSeriesId, SeveralPrimary>;

/// The series of one site: unique ids, sorted by id, and at most one Primary
/// series per (parameter, statistic). Built only by make; empty by default.
class SeriesSet {
 public:
  SeriesSet() = default;

  /// Sorts by id; then DuplicateSeriesId for the smallest repeated id, then
  /// SeveralPrimary for the first such pair in id order.
  [[nodiscard]] static std::expected<SeriesSet, SeriesSetError> make(
      std::vector<SeriesInfo> series);

  [[nodiscard]] std::span<const SeriesInfo> items() const& noexcept {
    return series_;
  }
  std::span<const SeriesInfo> items() const&& = delete;
  [[nodiscard]] std::size_t size() const noexcept { return series_.size(); }
  [[nodiscard]] bool empty() const noexcept { return series_.empty(); }

  /// The series with this id (a binary search), or nullptr. Points into the
  /// set.
  [[nodiscard]] const SeriesInfo* find(const TimeSeriesId& id) const& noexcept;
  const SeriesInfo* find(const TimeSeriesId& id) const&& = delete;

  /// The Primary series of the parameter and statistic, or nullptr. Points
  /// into the set.
  [[nodiscard]] const SeriesInfo* primary(
      const ParameterCode& parameter,
      const StatisticId& statistic) const& noexcept;
  const SeriesInfo* primary(const ParameterCode& parameter,
                            const StatisticId& statistic) const&& = delete;

  friend bool operator==(const SeriesSet&, const SeriesSet&) = default;

 private:
  explicit SeriesSet(std::vector<SeriesInfo> sorted)
      : series_{std::move(sorted)} {}

  std::vector<SeriesInfo> series_;
};

/// Why no series answers a request: none does (a missing id, or no Primary
/// series of the parameter and statistic: the fetch's NoData), or the
/// request names a series or statistic of the other collection.
enum class ResolveError : std::uint8_t { no_series, wrong_collection };

template <class C>
  requires std::same_as<C, Continuous> or std::same_as<C, Daily>
class Resolved;

/// Resolution, the first stage of a USGS fetch, over the series of the
/// request's site (`combined-metadata` by monitoring location). By
/// parameter: the Primary series of the parameter with statistic 00011
/// (continuous) or with the request's statistic, which must be a daily one.
/// By id: the series, which must belong to the request's collection.
[[nodiscard]] std::expected<Resolved<Continuous>, ResolveError> resolve(
    const Continuous& request, const SeriesSet& site);
[[nodiscard]] std::expected<Resolved<Daily>, ResolveError> resolve(
    const Daily& request, const SeriesSet& site);

/// The series a request of collection C resolved to. Resolution proves that
/// it exists at the station, belongs to C, and is the Primary one when chosen
/// by parameter. Only resolve() makes one, so it cannot be forged, and the
/// data stage takes a (C, Resolved<C>) pair, so a continuous request cannot
/// be paired with a daily series. Its quantity is a function of the
/// parameter (registry_quantity, else quantity_of), not a field.
template <class C>
  requires std::same_as<C, Continuous> or std::same_as<C, Daily>
class Resolved {
 public:
  [[nodiscard]] const SeriesInfo& series() const& noexcept { return series_; }
  const SeriesInfo& series() const&& = delete;

  friend bool operator==(const Resolved&, const Resolved&) = default;

 private:
  friend std::expected<Resolved<Continuous>, ResolveError> resolve(
      const Continuous& request, const SeriesSet& site);
  friend std::expected<Resolved<Daily>, ResolveError> resolve(
      const Daily& request, const SeriesSet& site);

  explicit Resolved(SeriesInfo series) : series_{std::move(series)} {}

  SeriesInfo series_;
};

/// The longest window one continuous request may span: 1000 days (the
/// server refuses more than 1100). Daily values have no limit (36 years came
/// back on one page), so there is no Daily overload.
[[nodiscard]] constexpr WindowSpan max_window(
    const Continuous& /*request*/) noexcept {
  return WindowSpan::of<1000, std::chrono::days>();
}

/// The grain continuous windows are aligned to: whole seconds (the API's
/// `datetime` has seconds).
inline constexpr WindowSpan grain = WindowSpan::of<1, std::chrono::seconds>();

/// A parameter's place in the quantity registry, with the datum its values
/// are relative to when the parameter implies one.
struct QuantityMapping {
  Quantity quantity;
  std::optional<VerticalDatum> datum;
  friend constexpr bool operator==(const QuantityMapping&,
                                   const QuantityMapping&) = default;
};

namespace detail {

struct ParameterRow {
  std::string_view code;
  QuantityMapping mapping;
};

// Each row checked against the parameter-codes collection (2026-10-15): its
// name, description and unit agree with the quantity's.
inline constexpr std::array parameter_rows{
    // "Temperature, water, degrees Celsius" (deg C).
    ParameterRow{.code = "00010",
                 .mapping = {.quantity = Quantity::water_temperature,
                             .datum = std::nullopt}},
    // "Temperature, air, degrees Celsius" (deg C).
    ParameterRow{.code = "00020",
                 .mapping = {.quantity = Quantity::air_temperature,
                             .datum = std::nullopt}},
    // "Wind speed, miles per hour" (mph).
    ParameterRow{
        .code = "00035",
        .mapping = {.quantity = Quantity::wind_speed, .datum = std::nullopt}},
    // "Wind direction, degrees clockwise from true north" (Deg).
    ParameterRow{.code = "00036",
                 .mapping = {.quantity = Quantity::wind_direction,
                             .datum = std::nullopt}},
    ParameterRow{
        .code = "00060",
        .mapping = {.quantity = Quantity::discharge, .datum = std::nullopt}},
    // Gage height: relative to the site's gage datum. STND is a station's
    // own local zero, whatever the agency calls it (datum.hpp).
    ParameterRow{.code = "00065",
                 .mapping = {.quantity = Quantity::water_level,
                             .datum = VerticalDatum::stnd}},
    ParameterRow{.code = "62620",
                 .mapping = {.quantity = Quantity::water_level,
                             .datum = VerticalDatum::navd88}},
    ParameterRow{.code = "62619",
                 .mapping = {.quantity = Quantity::water_level,
                             .datum = VerticalDatum::ngvd29}},
};

static_assert(std::ranges::all_of(parameter_rows, [](const ParameterRow& row) {
  return ParameterCode::make(row.code).has_value();
}));

}  // namespace detail

/// The registry quantity of a parameter: 00010 water_temperature; 00020
/// air_temperature; 00035 wind_speed; 00036 wind_direction; 00060
/// discharge; 00065 water_level relative to STND (the gage datum); 62620
/// water_level NAVD88; 62619 water_level NGVD29. nullopt for any other
/// parameter.
[[nodiscard]] constexpr std::optional<QuantityMapping> registry_quantity(
    const ParameterCode& parameter) noexcept {
  const auto row = std::ranges::find(detail::parameter_rows, parameter.view(),
                                     &detail::ParameterRow::code);
  if (row == detail::parameter_rows.end()) {
    return std::nullopt;
  }
  return row->mapping;
}

// quantity_of builds "usgs_<code>" without GenericQuantity::parse: the token
// is a valid name (a letter, then letters, digits and '_'), and no registry
// token starts with the prefix, so it never names a registry quantity.
static_assert(std::ranges::none_of(core::detail::quantity_registry,
                                   [](const QuantityInfo& row) {
                                     return row.token.starts_with("usgs_");
                                   }));

/// The quantity of a parameter's series: the registry quantity, or else the
/// generic quantity "usgs_<code>" (no standard name).
[[nodiscard]] QuantityId quantity_of(const ParameterCode& parameter);

}  // namespace mov::core::usgs
