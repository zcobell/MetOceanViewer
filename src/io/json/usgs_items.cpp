// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// usgs::read_continuous and read_daily: a page of a series' values
// (docs/providers-design.md sections 4.3 and 4.9).

#include <array>
#include <chrono>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/daily_series.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/overloaded.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/io/detail/json.hpp"
#include "mov/io/detail/normalize.hpp"
#include "mov/io/detail/reporting.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/detail/usgs_json.hpp"
#include "mov/io/error.hpp"
#include "mov/io/part.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/rfc3339.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/service.hpp"
#include "mov/io/wire/usgs.hpp"
#include "usgs_page.hpp"

namespace mov::io::wire::usgs {

namespace {

namespace json = io::detail::json;
using core::usgs::SeriesInfo;

// ---- what is kept
// ------------------------------------------------------------

// The most qualifiers one sample may carry (one seen).
constexpr std::size_t max_qualifiers = 16;

constexpr json::Keep qualifier_schema{.element = &json::leaf,
                                      .max_elements = max_qualifiers};
constexpr std::array value_fields{
    json::Field{.name = "time", .keep = &json::leaf},
    json::Field{.name = "value", .keep = &json::leaf},
    json::Field{.name = "approval_status", .keep = &json::leaf},
    json::Field{.name = "qualifier", .keep = &qualifier_schema}};
constexpr json::Keep value_schema{.fields = value_fields};
constexpr std::array feature_fields{
    json::Field{.name = "properties", .keep = &value_schema}};
constexpr json::Keep feature_schema{.fields = feature_fields};
constexpr json::Keep features_schema{.element = &feature_schema};
constexpr std::array page_fields{
    json::Field{.name = "features", .keep = &features_schema, .streamed = true},
    page::number_returned_field, page::links_field};
constexpr json::Keep page_schema{.fields = page_fields};

// ---- the series' metadata
// -----------------------------------------------------

std::unexpected<Error> inconsistent(std::string subject) {
  return io::detail::fail(io::detail::format_error(
      FormatErrc::inconsistent_metadata, std::move(subject)));
}

// The metadata of a series' values: its quantity, the parameter's name as
// the label, its unit, and the datum the parameter implies.
std::expected<core::SeriesMeta, Error> meta_of(const SeriesInfo& series) {
  core::SeriesMeta meta = core::SeriesMeta::make(
      {.quantity = core::usgs::quantity_of(series.parameter),
       .label = series.parameter_name,
       .unit = series.unit});
  const auto mapping = core::usgs::registry_quantity(series.parameter);
  if (not mapping or not mapping->datum) {
    return meta;
  }
  auto with_datum = meta.assume_datum(*mapping->datum);
  if (not with_datum) {
    return inconsistent(std::string{series.parameter.view()} + ":datum");
  }
  return *std::move(with_datum);
}

// ---- one feature
// --------------------------------------------------------------

// A value: null or "" is Missing; any other text must be one number.
std::expected<core::Sample, Error> sample_of(std::string_view body,
                                             const json::Value& properties) {
  const json::Value* value = json::member(properties, "value");
  if (value == nullptr) {
    return page::fail_at(body, ParseErrc::bad_member, properties.offset);
  }
  if (json::is_null(*value)) {
    return core::Sample{core::Missing{}};
  }
  const std::string* text = json::as_string(*value);
  if (text == nullptr) {
    return page::fail_at(body, ParseErrc::bad_member, value->offset);
  }
  if (text->empty()) {
    return core::Sample{core::Missing{}};
  }
  const auto number = io::detail::parse_double(*text);
  if (not number) {
    return page::fail_at(body,
                         number.error() == io::detail::NumberError::out_of_range
                             ? ParseErrc::out_of_range
                             : ParseErrc::bad_number,
                         value->offset);
  }
  return core::finite_or_missing(*number);
}

// The `time` string: bad_member when absent or no string.
std::expected<const json::Value*, Error> time_member(
    std::string_view body, const json::Value& properties) {
  const json::Value* time = json::member(properties, "time");
  if (time == nullptr) {
    return page::fail_at(body, ParseErrc::bad_member, properties.offset);
  }
  if (json::as_string(*time) == nullptr) {
    return page::fail_at(body, ParseErrc::bad_member, time->offset);
  }
  return time;
}

std::expected<core::Time, Error> instant_of(std::string_view body,
                                            const json::Value& properties) {
  const auto time = time_member(body, properties);
  if (not time) {
    return std::unexpected{time.error()};
  }
  const auto parsed = parse_rfc3339(*json::as_string(**time));
  if (not parsed) {
    return page::fail_at(body, ParseErrc::bad_date,
                         page::text_start(**time) + parsed.error().column);
  }
  return *parsed;
}

// A daily `time`: exactly "yyyy-mm-dd", the site's own calendar day.
std::expected<std::chrono::local_days, Error> day_of(
    std::string_view body, const json::Value& properties) {
  const auto time = time_member(body, properties);
  if (not time) {
    return std::unexpected{time.error()};
  }
  const std::string& text = *json::as_string(**time);
  core::DateTimeCursor cursor{text};
  const auto date = core::parse_date(cursor);
  if (not date or not cursor.at_end()) {
    const std::size_t column =
        date ? cursor.error_here().column : date.error().column;
    return page::fail_at(body, ParseErrc::bad_date,
                         page::text_start(**time) + column);
  }
  return std::chrono::local_days{date->time_since_epoch()};
}

// ---- pages
// -------------------------------------------------------------------

// The rows of a page as they come: a position (an instant or a day) and a
// sample each, and what the samples say about themselves.
template <class Position, class ReadPosition>
class Rows {
 public:
  Rows(std::string_view body, const ReadContext& ctx, ReadPosition read)
      : body_{body}, ctx_{ctx}, read_{read} {}

  std::expected<void, Error> add(std::size_t index,
                                 const json::Value& feature) {
    if (auto room = page::check_room(body_, index, feature, ctx_); not room) {
      return room;
    }
    const auto properties = page::properties_of(body_, feature);
    if (not properties) {
      return std::unexpected{properties.error()};
    }
    // Every member first: a row is taken whole or not at all.
    const auto position = read_(body_, **properties);
    if (not position) {
      return std::unexpected{position.error()};
    }
    const auto sample = sample_of(body_, **properties);
    if (not sample) {
      return std::unexpected{sample.error()};
    }
    if (auto marked = marks_.add(body_, **properties); not marked) {
      return marked;
    }
    positions_.push_back(*position);
    samples_.push_back(*sample);
    return {};
  }

  std::vector<Position>& positions() noexcept { return positions_; }
  core::Column& samples() noexcept { return samples_; }
  [[nodiscard]] const page::Marks& marks() const noexcept { return marks_; }
  [[nodiscard]] std::size_t size() const noexcept { return samples_.size(); }

 private:
  std::string_view body_;
  const ReadContext& ctx_;
  ReadPosition read_;
  std::vector<Position> positions_;
  core::Column samples_;
  page::Marks marks_;
};

// Parses a page, handing each feature to `rows`, and reads its paging.
template <class RowsT>
std::expected<Paging, Error> parse_page(std::string_view body,
                                        const ReadContext& ctx, RowsT& rows,
                                        Collection collection) {
  const auto document =
      json::parse(body, page_schema, ctx,
                  [&rows](std::size_t index, const json::Value& feature) {
                    return rows.add(index, feature);
                  });
  if (not document) {
    return std::unexpected{document.error()};
  }
  return page::read_paging(body, document->root, rows.size(), collection);
}

using InstantRows = Rows<core::Time, std::expected<core::Time, Error> (*)(
                                         std::string_view, const json::Value&)>;
using DayRows = Rows<std::chrono::local_days,
                     std::expected<std::chrono::local_days, Error> (*)(
                         std::string_view, const json::Value&)>;

std::expected<Read<ContinuousPage>, Error> continuous_page(
    std::string_view body, const SeriesInfo& series, const ReadContext& ctx) {
  InstantRows rows{body, ctx, &instant_of};
  auto paging = parse_page(body, ctx, rows, Collection::continuous);
  if (not paging) {
    return std::unexpected{std::move(paging).error()};
  }
  auto meta = meta_of(series);
  if (not meta) {
    return std::unexpected{std::move(meta).error()};
  }
  core::TimeAxis& times = rows.positions();
  std::array<core::Column*, 1> columns{&rows.samples()};
  const core::NormalizeReport report =
      io::detail::normalize_columns(times, columns);
  std::optional<TimeExtent> extent;
  if (not times.empty()) {
    extent = TimeExtent{.first = times.front(), .last = times.back()};
  }
  auto values = core::TimeSeries::make(
      std::move(times), std::move(rows.samples()), *std::move(meta));
  if (not values) {
    return inconsistent("usgs:continuous");
  }
  Read<ContinuousPage> out{
      .value = {.part = {}, .extent = extent, .paging = *std::move(paging)},
      .warnings = {}};
  if (not out.value.part.insert(*std::move(values))) {
    return inconsistent("usgs:continuous");
  }
  io::detail::add_normalize_warnings(out.warnings, report, "usgs:continuous");
  rows.marks().report(out.warnings);
  return out;
}

// A day as an instant, for normalize_columns: the same order, and back.
core::Time as_instant(std::chrono::local_days day) {
  return core::Time{std::chrono::duration_cast<std::chrono::milliseconds>(
      day.time_since_epoch())};
}
std::chrono::local_days as_day(core::Time t) {
  return std::chrono::local_days{
      std::chrono::floor<std::chrono::days>(t.time_since_epoch())};
}

std::expected<Read<DailyPage>, Error> daily_page(std::string_view body,
                                                 const SeriesInfo& series,
                                                 const ReadContext& ctx) {
  DayRows rows{body, ctx, &day_of};
  auto paging = parse_page(body, ctx, rows, Collection::daily);
  if (not paging) {
    return std::unexpected{std::move(paging).error()};
  }
  auto meta = meta_of(series);
  if (not meta) {
    return std::unexpected{std::move(meta).error()};
  }
  // The API serves days in feature-id order: sorted here, and only what is
  // dropped is reported.
  core::TimeAxis times;
  times.reserve(rows.size());
  for (const std::chrono::local_days day : rows.positions()) {
    times.push_back(as_instant(day));
  }
  std::array<core::Column*, 1> columns{&rows.samples()};
  core::NormalizeReport report = io::detail::normalize_columns(times, columns);
  report.descents = 0;
  std::vector<std::chrono::local_days> days;
  days.reserve(times.size());
  for (const core::Time t : times) {
    days.push_back(as_day(t));
  }
  auto values =
      core::DailySeries::make(*std::move(meta), std::move(days),
                              std::move(rows.samples()), series.statistic);
  if (not values) {
    return inconsistent("usgs:daily");
  }
  Read<DailyPage> out{
      .value = {.values = *std::move(values), .paging = *std::move(paging)},
      .warnings = {}};
  io::detail::add_normalize_warnings(out.warnings, report, "usgs:daily");
  rows.marks().report(out.warnings);
  return out;
}

template <class Page, class Parse>
std::expected<Reply<Page>, Error> classified(std::string_view body,
                                             std::string_view content_type,
                                             Parse parse) {
  return std::visit(
      core::Overloaded{
          [&](Payload) -> std::expected<Reply<Page>, Error> {
            return parse().transform(
                [](Read<Page> r) { return Reply<Page>{std::move(r)}; });
          },
          [](const ServiceError& e) -> std::expected<Reply<Page>, Error> {
            return Reply<Page>{e};
          }},
      classify_body(body, content_type));
}

}  // namespace

std::expected<Reply<ContinuousPage>, Error> read_continuous(
    std::string_view body, std::string_view content_type,
    const core::usgs::Resolved<core::usgs::Continuous>& series,
    const ReadContext& ctx) {
  return classified<ContinuousPage>(body, content_type, [&] {
    return io::detail::usgs::parse_continuous(body, series, ctx);
  });
}

std::expected<Reply<DailyPage>, Error> read_daily(
    std::string_view body, std::string_view content_type,
    const core::usgs::Resolved<core::usgs::Daily>& series,
    const ReadContext& ctx) {
  return classified<DailyPage>(body, content_type, [&] {
    return io::detail::usgs::parse_daily(body, series, ctx);
  });
}

}  // namespace mov::io::wire::usgs

namespace mov::io::detail::usgs {

const json::Keep& data_keep() { return wire::usgs::page_schema; }

std::expected<Read<wire::usgs::ContinuousPage>, Error> parse_continuous(
    std::string_view body,
    const core::usgs::Resolved<core::usgs::Continuous>& series,
    const ReadContext& ctx) {
  return wire::usgs::continuous_page(body, series.series(), ctx);
}

std::expected<Read<wire::usgs::DailyPage>, Error> parse_daily(
    std::string_view body,
    const core::usgs::Resolved<core::usgs::Daily>& series,
    const ReadContext& ctx) {
  return wire::usgs::daily_page(body, series.series(), ctx);
}

}  // namespace mov::io::detail::usgs
