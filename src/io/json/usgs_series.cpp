// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// usgs::read_series and read_catalog: combined-metadata rows as a site's
// series and as catalog rows (docs/providers-design.md sections 3.4, 4.3,
// 4.9 and 7).

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/geo.hpp"
#include "mov/core/overloaded.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/io/detail/json.hpp"
#include "mov/io/detail/usgs_json.hpp"
#include "mov/io/error.hpp"
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
using StationId = core::StationId<core::provider::Usgs>;

// ---- what is kept
// ------------------------------------------------------------

// A Point's coordinates: [lon, lat] or [lon, lat, height].
constexpr std::size_t max_coordinates = 3;

constexpr std::array row_fields{
    json::Field{.name = "id", .keep = &json::leaf},
    json::Field{.name = "parameter_code", .keep = &json::leaf},
    json::Field{.name = "parameter_name", .keep = &json::leaf},
    json::Field{.name = "statistic_id", .keep = &json::leaf},
    json::Field{.name = "primary", .keep = &json::leaf},
    json::Field{.name = "unit_of_measure", .keep = &json::leaf},
    json::Field{.name = "begin", .keep = &json::leaf},
    json::Field{.name = "end", .keep = &json::leaf},
    json::Field{.name = "monitoring_location_id", .keep = &json::leaf},
    json::Field{.name = "monitoring_location_name", .keep = &json::leaf}};
constexpr json::Keep row_schema{.fields = row_fields};
constexpr json::Keep coordinates_schema{.element = &json::leaf,
                                        .max_elements = max_coordinates};
constexpr std::array geometry_fields{
    json::Field{.name = "type", .keep = &json::leaf},
    json::Field{.name = "coordinates", .keep = &coordinates_schema}};
constexpr json::Keep geometry_schema{.fields = geometry_fields};
constexpr std::array feature_fields{
    json::Field{.name = "properties", .keep = &row_schema},
    json::Field{.name = "geometry", .keep = &geometry_schema}};
constexpr json::Keep feature_schema{.fields = feature_fields};
constexpr json::Keep features_schema{.element = &feature_schema};
constexpr std::array page_fields{
    json::Field{.name = "features", .keep = &features_schema, .streamed = true},
    page::number_returned_field, page::links_field};
constexpr json::Keep page_schema{.fields = page_fields};

// ---- why a row is left out
// ----------------------------------------------------

enum class Skip : std::uint8_t {
  id,
  parameter,
  statistic,
  dates,
  member,  // a member of the wrong JSON type
  station,
  name,
  position,
};

constexpr std::array skip_tokens{
    std::string_view{"id"},        std::string_view{"parameter"},
    std::string_view{"statistic"}, std::string_view{"dates"},
    std::string_view{"member"},    std::string_view{"station"},
    std::string_view{"name"},      std::string_view{"position"}};
static_assert(skip_tokens.size() ==
              static_cast<std::size_t>(Skip::position) + 1);

class Skipped {
 public:
  void add(Skip why) noexcept { ++counts_.at(static_cast<std::size_t>(why)); }

  void report(std::vector<Warning>& warnings) const {
    for (std::size_t i = 0; i < counts_.size(); ++i) {
      append_if_counted(warnings, {.code = WarningCode::series_skipped,
                                   .subject = std::string{skip_tokens.at(i)},
                                   .count = counts_.at(i)});
    }
  }

 private:
  std::array<std::size_t, skip_tokens.size()> counts_{};
};

template <class T>
using Kept = std::expected<T, Skip>;

// ---- units, read once per spelling
// --------------------------------------------

class Units {
 public:
  const std::optional<core::Unit>& of(std::optional<std::string_view> text) {
    const std::string key{text.value_or(std::string_view{})};
    auto it = cache_.find(key);
    if (it == cache_.end()) {
      it = cache_.emplace(key, unit_of_measure(text)).first;
    }
    for (const Warning& w : it->second.warnings) {
      unrecognized_.add(w.subject);
    }
    return it->second.value;
  }

  void report(std::vector<Warning>& warnings) const {
    unrecognized_.report(warnings, WarningCode::unrecognized_unit);
  }

 private:
  std::map<std::string, Read<std::optional<core::Unit>>, std::less<>> cache_;
  page::TextCounts unrecognized_;
};

// ---- one row
// ------------------------------------------------------------------

// A member that is absent or null (nullopt), or a string; another JSON type
// skips the row.
Kept<std::optional<std::string_view>> optional_text(const json::Value& object,
                                                    std::string_view name) {
  const json::Value* value = json::member(object, name);
  if (value == nullptr or json::is_null(*value)) {
    return std::optional<std::string_view>{};
  }
  const std::string* text = json::as_string(*value);
  if (text == nullptr) {
    return std::unexpected{Skip::member};
  }
  return std::optional<std::string_view>{*text};
}

// A code member: present, a string, and what `make` accepts; else `why`.
template <class Code>
Kept<Code> code_of(const json::Value& row, std::string_view name, Skip why) {
  const auto text = optional_text(row, name);
  if (not text) {
    return std::unexpected{text.error()};
  }
  if (not *text) {
    return std::unexpected{why};
  }
  const auto code = Code::make(**text);
  if (not code) {
    return std::unexpected{why};
  }
  return *code;
}

// `begin` or `end`: the UTC day of an RFC 3339 instant; null is unknown.
Kept<std::optional<std::chrono::sys_days>> day_of(const json::Value& row,
                                                  std::string_view name) {
  const auto text = optional_text(row, name);
  if (not text) {
    return std::unexpected{text.error()};
  }
  if (not *text) {
    return std::optional<std::chrono::sys_days>{};
  }
  const auto instant = parse_rfc3339(**text);
  if (not instant) {
    return std::unexpected{Skip::dates};
  }
  return std::optional<std::chrono::sys_days>{
      std::chrono::floor<std::chrono::days>(*instant)};
}

Kept<core::ValidRange> validity_of(const json::Value& row) {
  const auto first = day_of(row, "begin");
  if (not first) {
    return std::unexpected{first.error()};
  }
  const auto last = day_of(row, "end");
  if (not last) {
    return std::unexpected{last.error()};
  }
  const auto range = core::ValidRange::make({.first = *first, .last = *last});
  if (not range) {
    return std::unexpected{Skip::dates};
  }
  return *range;
}

Kept<SeriesInfo> series_of(const json::Value& row, Units& units) {
  const auto id = code_of<core::usgs::TimeSeriesId>(row, "id", Skip::id);
  if (not id) {
    return std::unexpected{id.error()};
  }
  const auto parameter = code_of<core::usgs::ParameterCode>(
      row, "parameter_code", Skip::parameter);
  if (not parameter) {
    return std::unexpected{parameter.error()};
  }
  const auto statistic =
      code_of<core::usgs::StatisticId>(row, "statistic_id", Skip::statistic);
  if (not statistic) {
    return std::unexpected{statistic.error()};
  }
  const auto name = optional_text(row, "parameter_name");
  const auto primary = optional_text(row, "primary");
  const auto unit = optional_text(row, "unit_of_measure");
  if (not name or not primary or not unit) {
    return std::unexpected{Skip::member};
  }
  const auto validity = validity_of(row);
  if (not validity) {
    return std::unexpected{validity.error()};
  }
  return SeriesInfo{
      .id = *id,
      .parameter = *parameter,
      .statistic = *statistic,
      .parameter_name = std::string{name->value_or(std::string_view{})},
      .primary = primary->value_or(std::string_view{}) == "Primary",
      .unit = units.of(*unit),
      .validity = *validity};
}

// A GeoJSON Point `[lon, lat]` (a height after them is ignored).
Kept<core::Location> location_of(const json::Value& feature) {
  const json::Value* geometry = json::member(feature, "geometry");
  if (geometry == nullptr or json::as_object(*geometry) == nullptr) {
    return std::unexpected{Skip::position};
  }
  const json::Value* type = json::member(*geometry, "type");
  const json::Value* coordinates = json::member(*geometry, "coordinates");
  const std::string* kind = type == nullptr ? nullptr : json::as_string(*type);
  const json::Array* point =
      coordinates == nullptr ? nullptr : json::as_array(*coordinates);
  if (kind == nullptr or *kind != "Point" or point == nullptr or
      point->size() < 2) {
    return std::unexpected{Skip::position};
  }
  const std::optional<double> lon = json::as_double((*point)[0]);
  const std::optional<double> lat = json::as_double((*point)[1]);
  if (not lon or not lat) {
    return std::unexpected{Skip::position};
  }
  const auto location = core::Location::make({.lat = *lat, .lon = *lon});
  if (not location) {
    return std::unexpected{Skip::position};
  }
  return *location;
}

Kept<SiteSeries> site_series_of(const json::Value& feature,
                                const json::Value& row, Units& units) {
  const auto raw_id = optional_text(row, "monitoring_location_id");
  if (not raw_id) {
    return std::unexpected{raw_id.error()};
  }
  const auto station = StationId::make(raw_id->value_or(std::string_view{}));
  if (not station) {
    return std::unexpected{Skip::station};
  }
  const auto name_text = optional_text(row, "monitoring_location_name");
  if (not name_text) {
    return std::unexpected{name_text.error()};
  }
  auto name = core::StationText::make(
      std::string{name_text->value_or(std::string_view{})});
  if (not name) {
    return std::unexpected{Skip::name};
  }
  const auto location = location_of(feature);
  if (not location) {
    return std::unexpected{location.error()};
  }
  auto series = series_of(row, units);
  if (not series) {
    return std::unexpected{series.error()};
  }
  return SiteSeries{.station = *station,
                    .name = *std::move(name),
                    .location = *location,
                    .series = *std::move(series)};
}

// ---- pages
// -------------------------------------------------------------------

// Reads a combined-metadata page, each row by `row_of`.
template <class Row, class RowOf>
std::expected<Read<std::pair<std::vector<Row>, Paging>>, Error> rows_page(
    std::string_view body, const ReadContext& ctx, RowOf row_of) {
  std::vector<Row> rows;
  Skipped skipped;
  Units units;
  std::size_t features = 0;
  const auto document = json::parse(
      body, page_schema, ctx,
      [&](std::size_t index,
          const json::Value& feature) -> std::expected<void, Error> {
        if (auto room = page::check_room(body, index, feature, ctx); not room) {
          return room;
        }
        const auto properties = page::properties_of(body, feature);
        if (not properties) {
          return std::unexpected{properties.error()};
        }
        ++features;
        auto row = row_of(feature, **properties, units);
        if (row) {
          rows.push_back(*std::move(row));
        } else {
          skipped.add(row.error());
        }
        return {};
      });
  if (not document) {
    return std::unexpected{document.error()};
  }
  auto paging = page::read_paging(body, document->root, features,
                                  Collection::combined_metadata);
  if (not paging) {
    return std::unexpected{std::move(paging).error()};
  }
  Read<std::pair<std::vector<Row>, Paging>> out{
      .value = {std::move(rows), *std::move(paging)}, .warnings = {}};
  skipped.report(out.warnings);
  units.report(out.warnings);
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

std::expected<Reply<SeriesPage>, Error> read_series(
    std::string_view body, std::string_view content_type,
    const ReadContext& ctx) {
  return classified<SeriesPage>(body, content_type, [&] {
    return io::detail::usgs::parse_series(body, ctx);
  });
}

std::expected<Reply<CatalogPage>, Error> read_catalog(
    std::string_view body, std::string_view content_type,
    const ReadContext& ctx) {
  return classified<CatalogPage>(body, content_type, [&] {
    return io::detail::usgs::parse_catalog(body, ctx);
  });
}

}  // namespace mov::io::wire::usgs

namespace mov::io::detail::usgs {

const json::Keep& series_keep() { return wire::usgs::page_schema; }

std::expected<Read<wire::usgs::SeriesPage>, Error> parse_series(
    std::string_view body, const ReadContext& ctx) {
  return wire::usgs::rows_page<core::usgs::SeriesInfo>(
             body, ctx,
             [](const json::Value& /*feature*/, const json::Value& row,
                wire::usgs::Units& units) {
               return wire::usgs::series_of(row, units);
             })
      .transform([](auto r) {
        return std::move(r).transform([](auto rows_and_paging) {
          return wire::usgs::SeriesPage{
              .series = std::move(rows_and_paging.first),
              .paging = std::move(rows_and_paging.second)};
        });
      });
}

std::expected<Read<wire::usgs::CatalogPage>, Error> parse_catalog(
    std::string_view body, const ReadContext& ctx) {
  return wire::usgs::rows_page<wire::usgs::SiteSeries>(
             body, ctx,
             [](const json::Value& feature, const json::Value& row,
                wire::usgs::Units& units) {
               return wire::usgs::site_series_of(feature, row, units);
             })
      .transform([](auto r) {
        return std::move(r).transform([](auto rows_and_paging) {
          return wire::usgs::CatalogPage{
              .rows = std::move(rows_and_paging.first),
              .paging = std::move(rows_and_paging.second)};
        });
      });
}

}  // namespace mov::io::detail::usgs
