// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The requests of the USGS Water Data API (docs/providers-design.md section
// 4.1) and its unit spellings.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <ratio>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/overloaded.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/core/windows.hpp"
#include "mov/io/detail/civil_time.hpp"
#include "mov/io/detail/reporting.hpp"
#include "mov/io/read.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/io/wire/usgs.hpp"

namespace mov::io::wire::usgs {

namespace {

using Station = core::StationId<core::provider::Usgs>;

// What resolution and the series picker read of a series.
constexpr std::string_view series_properties =
    "id,parameter_code,parameter_name,statistic_id,primary,unit_of_measure,"
    "begin,end";
// The catalog reads its monitoring location too.
constexpr std::string_view catalog_properties =
    "monitoring_location_id,monitoring_location_name,id,parameter_code,"
    "parameter_name,statistic_id,primary,unit_of_measure,begin,end";
// What a data page reads of a value.
constexpr std::string_view value_properties =
    "time,value,approval_status,qualifier";

QueryParam param(std::string_view name, std::string_view value) {
  return {.name = std::string{name}, .value = std::string{value}};
}

// The parameters every request ends with: the page size and JSON (CSV has
// no next link).
void append_page(std::vector<QueryParam>& query) {
  query.push_back(param("limit", std::to_string(page_rows)));
  query.push_back(param("f", "json"));
}

Composed series_request(std::vector<QueryParam> selection) {
  selection.push_back(param("properties", series_properties));
  selection.push_back(param("skipGeometry", "true"));
  append_page(selection);
  return {.origin = Origin::usgs,
          .path = std::string{items_path(Collection::combined_metadata)},
          .query = std::move(selection)};
}

std::vector<QueryParam> by_parameter(const Station& station,
                                     const core::usgs::ParameterCode& parameter,
                                     const core::usgs::StatisticId& statistic) {
  return {param("monitoring_location_id", station.value()),
          param("parameter_code", parameter.view()),
          param("statistic_id", statistic.view())};
}

std::vector<QueryParam> by_id(const Station& station,
                              const core::usgs::TimeSeriesId& id) {
  return {param("monitoring_location_id", station.value()),
          param("id", id.view())};
}

// `yyyy-mm-ddThh:mm:ssZ`; the caller keeps t within the four-digit years.
std::string wire_second(core::Time t) {
  const io::detail::CivilTime f = io::detail::civil_fields(t);
  return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", f.year, f.month,
                     f.day, f.hour, f.minute, f.second);
}

// `yyyy-mm-dd` of a day within the four-digit years.
std::string wire_day(std::chrono::local_days day) {
  const std::chrono::year_month_day ymd{
      std::chrono::sys_days{day.time_since_epoch()}};
  return std::format("{:04}-{:02}-{:02}", static_cast<int>(ymd.year()),
                     static_cast<unsigned>(ymd.month()),
                     static_cast<unsigned>(ymd.day()));
}

std::expected<std::string, TargetError> instants(core::TimeRange window) {
  // 64-bit seconds: std::chrono::seconds is wide enough everywhere, but the
  // bound check below keeps the arithmetic far from its ends anyway.
  using seconds = std::chrono::duration<std::int64_t>;
  if (window.begin() < io::detail::imeds_first or
      window.end() > io::detail::imeds_end) {
    return std::unexpected{
        TargetError{.reason = TargetErrc::outside_four_digit_years}};
  }
  const core::Time first = std::chrono::ceil<seconds>(window.begin());
  const core::Time last = std::chrono::ceil<seconds>(window.end()) - seconds{1};
  if (last < first) {
    return std::unexpected{TargetError{.reason = TargetErrc::no_whole_second}};
  }
  return wire_second(first) + "/" + wire_second(last);
}

// The first and the last day of a range, within the four-digit years.
std::expected<std::string, TargetError> dates(core::DayRange range) {
  using std::chrono::January;
  using std::chrono::local_days;
  using std::chrono::year;
  constexpr local_days first_day{year{0} / January / 1};
  constexpr local_days end_day{year{9999} / std::chrono::December / 31};
  const local_days last = range.end() - std::chrono::days{1};
  if (range.first() < first_day or last > end_day) {
    return std::unexpected{
        TargetError{.reason = TargetErrc::outside_four_digit_years}};
  }
  return wire_day(range.first()) + "/" + wire_day(last);
}

Composed data_request(Collection collection, const core::usgs::TimeSeriesId& id,
                      std::string_view datetime) {
  std::vector<QueryParam> query{
      param("time_series_id", id.view()),
      param("datetime", datetime),
      param("properties", value_properties),
      param("skipGeometry", "true"),
  };
  append_page(query);
  return {.origin = Origin::usgs,
          .path = std::string{items_path(collection)},
          .query = std::move(query)};
}

// ---- units
// ---------------------------------------------------------------------

struct Alias {
  std::string_view usgs;
  std::string_view core;
};

// USGS spellings parse_unit does not know, as core spells them (recorded
// 2026-10-15: `ft^3/s` and `m^3/s` in combined-metadata; `ft/sec`, `m3/sec`
// and `Deg` in parameter-codes).
constexpr std::array unit_aliases{
    Alias{.usgs = "ft^3/s", .core = "ft3/s"},
    Alias{.usgs = "m^3/s", .core = "m3/s"},
    Alias{.usgs = "ft/sec", .core = "ft/s"},
    Alias{.usgs = "m3/sec", .core = "m3/s"},
    Alias{.usgs = "Deg", .core = "degree"},
};

}  // namespace

Composed resolve_target(const core::usgs::Continuous& request) {
  return series_request(std::visit(
      core::Overloaded{[&](const core::usgs::ByParameter& p) {
                         return by_parameter(
                             request.station, p.parameter,
                             core::usgs::StatisticId::instantaneous());
                       },
                       [&](const core::usgs::ByTimeSeries& t) {
                         return by_id(request.station, t.id);
                       }},
      request.series));
}

Composed resolve_target(const core::usgs::Daily& request) {
  return series_request(std::visit(
      core::Overloaded{[&](const core::usgs::DailyByParameter& p) {
                         return by_parameter(request.station, p.parameter,
                                             p.statistic);
                       },
                       [&](const core::usgs::ByTimeSeries& t) {
                         return by_id(request.station, t.id);
                       }},
      request.series));
}

Composed site_series_target(const Station& station) {
  return series_request({param("monitoring_location_id", station.value()),
                         param("data_type", "Continuous values,Daily values")});
}

Composed catalog_target() {
  std::string parameters;
  for (const std::string_view code : map_parameters) {
    parameters += parameters.empty() ? "" : ",";
    parameters += code;
  }
  constexpr core::usgs::StatisticId instantaneous =
      core::usgs::StatisticId::instantaneous();
  std::vector<QueryParam> query{
      param("parameter_code", parameters),
      param("statistic_id", instantaneous.view()),
      param("properties", catalog_properties),
  };
  append_page(query);
  return {.origin = Origin::usgs,
          .path = std::string{items_path(Collection::combined_metadata)},
          .query = std::move(query)};
}

std::expected<Composed, TargetError> data_target(
    const core::usgs::Resolved<core::usgs::Continuous>& series,
    core::TimeRange window) {
  return instants(window).transform([&](const std::string& datetime) {
    return data_request(Collection::continuous, series.series().id, datetime);
  });
}

std::expected<Composed, TargetError> data_target(
    const core::usgs::Resolved<core::usgs::Daily>& series,
    core::DayRange days) {
  return dates(days).transform([&](const std::string& datetime) {
    return data_request(Collection::daily, series.series().id, datetime);
  });
}

Read<std::optional<core::Unit>> unit_of_measure(
    std::optional<std::string_view> text) {
  if (text) {
    const auto alias = std::ranges::find(unit_aliases, *text, &Alias::usgs);
    if (alias != unit_aliases.end()) {
      return pure(core::parse_unit(alias->core));
    }
  }
  return io::detail::parsed_unit(text);
}

}  // namespace mov::io::wire::usgs
