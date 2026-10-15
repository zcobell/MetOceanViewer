// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

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
#include "mov/core/provider/coops.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/io/detail/civil_time.hpp"
#include "mov/io/wire/coops.hpp"
#include "mov/io/wire/origin.hpp"

namespace mov::io::wire::coops {

namespace {

using Station = core::StationId<core::provider::Coops>;

constexpr std::string_view datagetter_path = "/api/prod/datagetter";
constexpr std::string_view metadata_path = "/mdapi/prod/webapi";

// begin_date and end_date: the first and the last whole minute of a window.
struct DateParams {
  std::string begin;
  std::string end;
};

// `yyyyMMdd HH:mm`; the caller keeps t within the four-digit years.
std::string wire_minute(core::Time t) {
  const detail::CivilTime f = detail::civil_fields(t);
  return std::format("{:04}{:02}{:02} {:02}:{:02}", f.year, f.month, f.day,
                     f.hour, f.minute);
}

std::expected<DateParams, TargetError> date_params(core::TimeRange window) {
  // 64-bit minutes: std::chrono::minutes may be 32-bit (MSVC), too narrow
  // for year 9999.
  using minutes = std::chrono::duration<std::int64_t, std::ratio<60>>;
  // Within these bounds the minute arithmetic below cannot overflow, and
  // every whole minute of the window has a four-digit year.
  if (window.begin() < detail::imeds_first or
      window.end() > detail::imeds_end) {
    return std::unexpected{TargetError{
        .reason = TargetErrc::outside_four_digit_years, .window = window}};
  }
  const core::Time first =
      std::chrono::ceil<minutes>(window.begin());  // at or after the begin
  const core::Time last =
      std::chrono::ceil<minutes>(window.end()) - minutes{1};  // before the end
  if (last < first) {
    return std::unexpected{
        TargetError{.reason = TargetErrc::no_whole_minute, .window = window}};
  }
  return DateParams{.begin = wire_minute(first), .end = wire_minute(last)};
}

QueryParam param(std::string_view name, std::string_view value) {
  return {.name = std::string{name}, .value = std::string{value}};
}

Composed data_target(const Station& station, const Series& series,
                     const DateParams& dates) {
  std::vector<QueryParam> query{
      param("product", product_name(series)),
      param("application", "MetOceanViewer"),
      param("station", station.value()),
      param("begin_date", dates.begin),
      param("end_date", dates.end),
  };
  if (const auto datum = datum_of(series)) {
    query.push_back(param("datum", core::coops::wire_token(*datum)));
  }
  if (const auto interval = interval_token(series)) {
    query.push_back(param("interval", *interval));
  }
  query.push_back(param("time_zone", "gmt"));
  query.push_back(param("units", "metric"));
  query.push_back(param("format", "json"));
  return {.origin = Origin::coops_data,
          .path = std::string{datagetter_path},
          .query = std::move(query)};
}

std::string_view list_type(StationList list) noexcept {
  switch (list) {
    case StationList::water_levels:
      return "waterlevels";
    case StationList::met:
      return "met";
    case StationList::air_gap:
      return "airgap";
    case StationList::tide_predictions:
      break;
  }
  return "tidepredictions";
}

}  // namespace

std::expected<WindowTargets, TargetError> targets(
    const Station& station, const core::coops::Product& product,
    core::TimeRange window) {
  return date_params(window).transform([&](const DateParams& dates) {
    return std::visit(
        core::Overloaded{
            [&](const Single& s) -> WindowTargets {
              return SingleTarget{
                  .series = s.series,
                  .target = data_target(station, s.series, dates)};
            },
            [&](const Pair& p) -> WindowTargets {
              return PairTargets{
                  .pair = p,
                  .observed = data_target(station, p.observed(), dates),
                  .predicted = data_target(station, p.predicted(), dates)};
            }},
        series_of(product));
  });
}

Composed station_list_target(StationList list) {
  std::vector<QueryParam> query{param("type", list_type(list))};
  if (list != StationList::tide_predictions) {
    query.push_back(param("expand", "details,sensors"));
  }
  return {.origin = Origin::coops_metadata,
          .path = std::format("{}/stations.json", metadata_path),
          .query = std::move(query)};
}

Composed datums_target(const Station& station) {
  return {.origin = Origin::coops_metadata,
          .path = std::format("{}/stations/{}/datums.json", metadata_path,
                              station.value()),
          .query = {param("units", "metric")}};
}

}  // namespace mov::io::wire::coops
