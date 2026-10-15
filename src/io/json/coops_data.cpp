// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// coops::parse_data: a datagetter body to its columns
// (docs/providers-design.md section 4.3).

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "document.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/overloaded.hpp"
#include "mov/core/provider/coops.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/io/detail/reporting.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/coops.hpp"

namespace mov::io::wire::coops {

namespace {

using json::Body;
using json::Json;

// A quantity outside the registry: its token, CF standard name and label.
struct Generic {
  std::string_view token;
  std::string_view standard_name;
  std::string_view label;
};

// One column of a body: the record member it comes from, what it is, and the
// unit CO-OPS delivers it in with units=metric.
struct ColumnSpec {
  std::string_view member;
  std::variant<core::Quantity, Generic> quantity;
  std::string_view unit;
};

using core::Quantity;

constexpr std::array observed_columns{
    ColumnSpec{.member = "v", .quantity = Quantity::water_level, .unit = "m"}};
constexpr std::array predicted_columns{ColumnSpec{
    .member = "v", .quantity = Quantity::water_level_prediction, .unit = "m"}};
constexpr std::array wind_columns{
    ColumnSpec{.member = "s", .quantity = Quantity::wind_speed, .unit = "m/s"},
    ColumnSpec{
        .member = "d", .quantity = Quantity::wind_direction, .unit = "degree"},
    ColumnSpec{.member = "g", .quantity = Quantity::wind_gust, .unit = "m/s"}};
constexpr std::array air_temperature_columns{ColumnSpec{
    .member = "v", .quantity = Quantity::air_temperature, .unit = "degC"}};
constexpr std::array water_temperature_columns{ColumnSpec{
    .member = "v", .quantity = Quantity::water_temperature, .unit = "degC"}};
constexpr std::array air_pressure_columns{ColumnSpec{
    .member = "v", .quantity = Quantity::air_pressure, .unit = "hPa"}};
constexpr std::array humidity_columns{ColumnSpec{
    .member = "v", .quantity = Quantity::relative_humidity, .unit = "percent"}};
constexpr std::array conductivity_columns{ColumnSpec{
    .member = "v", .quantity = Quantity::conductivity, .unit = "mS/cm"}};
constexpr std::array salinity_columns{ColumnSpec{
    .member = "s",
    .quantity = Generic{.token = "salinity",
                        .standard_name = "sea_water_practical_salinity",
                        .label = "Salinity"},
    .unit = "PSU"}};
constexpr std::array visibility_columns{
    ColumnSpec{.member = "v", .quantity = Quantity::visibility, .unit = "km"}};
constexpr std::array air_gap_columns{ColumnSpec{
    .member = "v",
    .quantity =
        Generic{.token = "air_gap", .standard_name = "", .label = "Air gap"},
    .unit = "m"}};

std::span<const ColumnSpec> met_columns(core::coops::Met product) noexcept {
  using core::coops::Met;
  switch (product) {
    case Met::air_temperature:
      return air_temperature_columns;
    case Met::water_temperature:
      return water_temperature_columns;
    case Met::wind:
      return wind_columns;
    case Met::air_pressure:
      return air_pressure_columns;
    case Met::humidity:
      return humidity_columns;
    case Met::conductivity:
      return conductivity_columns;
    case Met::salinity:
      return salinity_columns;
    case Met::visibility:
      return visibility_columns;
    case Met::air_gap:
      break;
  }
  return air_gap_columns;
}

// What a body of `series` holds: its array member, its columns and the
// datum of a water level.
struct Layout {
  std::string_view array_key;
  std::span<const ColumnSpec> columns;
  std::optional<core::VerticalDatum> datum;
};

Layout layout_of(const Series& series) {
  return std::visit(
      core::Overloaded{
          [](Observation o) {
            return Layout{.array_key = "data",
                          .columns = observed_columns,
                          .datum = core::coops::to_vertical(o.datum.get())};
          },
          [](Prediction p) {
            return Layout{.array_key = "predictions",
                          .columns = predicted_columns,
                          .datum = core::coops::to_vertical(p.datum.get())};
          },
          [](core::coops::Meteorological m) {
            return Layout{.array_key = "data",
                          .columns = met_columns(m.product),
                          .datum = std::nullopt};
          }},
      series);
}

// The metadata of a column, with the unit's warning.
Read<core::SeriesMeta> meta_of(const ColumnSpec& spec,
                               std::optional<core::VerticalDatum> datum) {
  const auto [quantity, label] = std::visit(
      core::Overloaded{
          [](Quantity q) {
            return std::pair{core::QuantityId{q},
                             std::string{core::info(q).long_name}};
          },
          [](const Generic& g) {
            // The tokens of the table are valid names outside the registry
            // (pinned by a test).
            return std::pair{
                core::QuantityId{
                    core::GenericQuantity::parse(
                        {.token = g.token, .standard_name = g.standard_name})
                        .value_or(core::GenericQuantity{})},
                std::string{g.label}};
          }},
      spec.quantity);
  return detail::parsed_unit(spec.unit).transform(
      [&](std::optional<core::Unit> unit) {
        const core::SeriesMeta meta = core::SeriesMeta::make(
            {.quantity = quantity, .label = label, .unit = std::move(unit)});
        // Water levels carry a datum; their quantities take one.
        return datum ? meta.assume_datum(*datum).value_or(meta) : meta;
      });
}

constexpr std::size_t value_column_offset = 1;  // past the opening quote

// A record's time: core::parse_utc_datetime over `t`, its error at the
// character it names.
std::expected<core::Time, ParseError> time_of(const Body& body,
                                              const Json& record) {
  const auto text = json::string_member(body, record, "t");
  if (not text) {
    return std::unexpected{text.error()};
  }
  const auto time = core::parse_utc_datetime(*text);
  if (not time) {
    const Json* t = json::find_member(record, "t");
    const std::size_t at =
        (t == nullptr ? record.start_pos() : t->start_pos()) +
        value_column_offset + time.error().column;
    return std::unexpected{body.error_at(ParseErrc::bad_date, at)};
  }
  return *time;
}

// A value: "" is Missing; any other text must be one number.
std::expected<core::Sample, ParseError> sample_of(const Body& body,
                                                  const Json& record,
                                                  std::string_view member) {
  const auto text = json::string_member(body, record, member);
  if (not text) {
    return std::unexpected{text.error()};
  }
  if (text->empty()) {
    return core::Sample{core::Missing{}};
  }
  const auto value = detail::parse_double(*text);
  if (not value) {
    const Json* at = json::find_member(record, member);
    return std::unexpected{
        body.error_at(value.error() == detail::NumberError::out_of_range
                          ? ParseErrc::out_of_range
                          : ParseErrc::bad_number,
                      at == nullptr ? record : *at)};
  }
  return core::finite_or_missing(*value);
}

// A quality flag string ("0,0,0,0") with a nonzero flag in it.
bool flagged(std::string_view flags) noexcept {
  return std::ranges::any_of(
      flags, [](char c) { return c != '0' and c != ',' and c != ' '; });
}

class DataReader {
 public:
  DataReader(const Body& body, const Series& series, const ReadContext& ctx)
      : body_{body},
        layout_{layout_of(series)},
        ctx_{ctx},
        rows_(layout_.columns.size()) {}

  std::expected<Read<Columns>, Error> read() && {
    const auto document = json::parse_streaming(
        body_, layout_.array_key, ctx_,
        [this](std::size_t index, Json& record) { return add(index, record); });
    if (not document) {
      return std::unexpected{document.error()};
    }
    const Json* array = json::find_member(*document, layout_.array_key);
    if (array == nullptr or not array->is_array()) {
      return detail::fail(body_.error_at(
          ParseErrc::bad_member, array == nullptr ? *document : *array));
    }
    return std::move(*this).columns();
  }

 private:
  std::expected<void, Error> add(std::size_t index, const Json& record) {
    const std::size_t samples = (index + 1) * layout_.columns.size();
    if (samples > ctx_.limits.max_elements) {
      return detail::fail(
          ParseError::make(ParseErrc::too_large, body_.where(record),
                           std::to_string(samples) + " elements, limit " +
                               std::to_string(ctx_.limits.max_elements)));
    }
    if (not record.is_object()) {
      return detail::fail(body_.error_at(ParseErrc::bad_member, record));
    }
    const auto time = time_of(body_, record);
    if (not time) {
      return detail::fail(time.error());
    }
    for (std::size_t c = 0; c < layout_.columns.size(); ++c) {
      const auto sample = sample_of(body_, record, layout_.columns[c].member);
      if (not sample) {
        return detail::fail(sample.error());
      }
      rows_[c].push_back({.time = *time, .sample = *sample});
    }
    return count_marks(record);
  }

  std::expected<void, Error> count_marks(const Json& record) {
    const auto flags = json::optional_string_member(body_, record, "f");
    if (not flags) {
      return detail::fail(flags.error());
    }
    const auto quality = json::optional_string_member(body_, record, "q");
    if (not quality) {
      return detail::fail(quality.error());
    }
    if (*flags and flagged(**flags)) {
      ++suspect_;
    }
    if (quality->value_or(std::string_view{}) == "p") {
      ++provisional_;
    }
    return {};
  }

  Read<Columns> columns() && {
    Read<Columns> out{.value = {}, .warnings = {}};
    for (std::size_t c = 0; c < layout_.columns.size(); ++c) {
      Read<core::SeriesMeta> meta = meta_of(layout_.columns[c], layout_.datum);
      append(out.warnings, std::move(meta.warnings));
      const std::string token{core::token(meta.value.quantity())};
      core::Normalized normalized =
          core::normalize(std::move(rows_[c]), std::move(meta.value));
      const std::string subject = "coops:" + token;
      append_if_counted(out.warnings, {.code = WarningCode::times_reordered,
                                       .subject = subject,
                                       .count = normalized.report.descents});
      append_if_counted(out.warnings,
                        {.code = WarningCode::duplicate_times_dropped,
                         .subject = subject,
                         .count = normalized.report.duplicates_dropped});
      append_if_counted(out.warnings,
                        {.code = WarningCode::conflicting_duplicate_times,
                         .subject = subject,
                         .count = normalized.report.conflicting_duplicates});
      out.value.insert_or_assign(token, std::move(normalized.series));
    }
    append_if_counted(out.warnings, {.code = WarningCode::suspect_samples_kept,
                                     .subject = "coops:f",
                                     .count = suspect_});
    append_if_counted(out.warnings, {.code = WarningCode::provisional_samples,
                                     .subject = "coops:q",
                                     .count = provisional_});
    return out;
  }

  const Body& body_;
  Layout layout_;
  const ReadContext& ctx_;
  std::vector<std::vector<core::Point>> rows_;
  std::size_t suspect_ = 0;
  std::size_t provisional_ = 0;
};

}  // namespace

std::expected<Read<Columns>, Error> parse_data(std::string_view body,
                                               const Series& series,
                                               const ReadContext& ctx) {
  const Body text{body};
  return DataReader{text, series, ctx}.read();
}

}  // namespace mov::io::wire::coops
