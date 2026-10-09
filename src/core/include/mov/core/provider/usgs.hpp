// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "mov/core/ascii.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/overloaded.hpp"
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

/// What resolution (the first stage of a USGS fetch) proves about the series
/// a request selects: it exists at the station, belongs to the request's
/// collection, and is the Primary one when chosen by parameter. Its quantity
/// is not stored: it is registry_quantity(parameter), or quantity_of for a
/// parameter outside the registry.
struct ResolvedSeries {
  TimeSeriesId id;
  ParameterCode parameter;
  StatisticId statistic;
  std::string parameter_name;
  std::optional<Unit> unit;
  friend bool operator==(const ResolvedSeries&,
                         const ResolvedSeries&) = default;
};

/// One series of a site: a row of Capabilities<Usgs> (the station catalog)
/// and of the series picker.
struct SeriesInfo {
  TimeSeriesId id;
  ParameterCode parameter;
  StatisticId statistic;
  bool primary;
  std::optional<Unit> unit;
  ValidRange validity;
  friend bool operator==(const SeriesInfo&, const SeriesInfo&) = default;
};

/// The longest window one request may span: 1000 days for continuous values
/// (the server refuses more than 1100), none for daily values (36 years came
/// back on one page).
[[nodiscard]] constexpr std::optional<std::chrono::days> max_window(
    const Request& request) noexcept {
  return std::visit(Overloaded{[](const Continuous&) {
                                 return std::optional{std::chrono::days{1000}};
                               },
                               [](const Daily&) {
                                 return std::optional<std::chrono::days>{};
                               }},
                    request);
}

/// The grain continuous windows are aligned to: whole seconds (the API's
/// `datetime` has seconds).
inline constexpr std::chrono::seconds grain{1};

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

// Further rows (00010, 00020, 00035, 00036) only after P3 checks them against
// the parameter-codes collection.
inline constexpr std::array parameter_rows{
    ParameterRow{
        .code = "00060",
        .mapping = {.quantity = Quantity::discharge, .datum = std::nullopt}},
    // Gage height: relative to the gage datum, which the core calls STND.
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

/// The registry quantity of a parameter: 00060 discharge; 00065 water_level
/// relative to STND (the gage datum); 62620 water_level NAVD88; 62619
/// water_level NGVD29. nullopt for any other parameter.
[[nodiscard]] constexpr std::optional<QuantityMapping> registry_quantity(
    const ParameterCode& parameter) noexcept {
  const auto row = std::ranges::find(detail::parameter_rows, parameter.view(),
                                     &detail::ParameterRow::code);
  if (row == detail::parameter_rows.end()) {
    return std::nullopt;
  }
  return row->mapping;
}

/// The quantity of a parameter's series: the registry quantity, or else the
/// generic quantity "usgs_<code>" (no standard name).
[[nodiscard]] QuantityId quantity_of(const ParameterCode& parameter);

}  // namespace mov::core::usgs
