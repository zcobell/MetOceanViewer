// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// coops::read_data: a datagetter body to its columns
// (docs/providers-design.md sections 4.3 and 4.8).

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/overloaded.hpp"
#include "mov/core/provider/coops.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/io/detail/coops_json.hpp"
#include "mov/io/detail/json.hpp"
#include "mov/io/detail/normalize.hpp"
#include "mov/io/detail/reporting.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/detail/text_position.hpp"
#include "mov/io/error.hpp"
#include "mov/io/part.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/coops.hpp"
#include "mov/io/wire/service.hpp"

namespace mov::io::wire::coops {

namespace {

namespace json = io::detail::json;
using core::Quantity;

// ---- what is kept
// ------------------------------------------------------------

constexpr std::array record_fields{
    json::Field{.name = "t", .keep = &json::leaf},
    json::Field{.name = "v", .keep = &json::leaf},
    json::Field{.name = "s", .keep = &json::leaf},
    json::Field{.name = "d", .keep = &json::leaf},
    json::Field{.name = "g", .keep = &json::leaf},
    json::Field{.name = "f", .keep = &json::leaf},
    json::Field{.name = "q", .keep = &json::leaf}};
constexpr json::Keep record_schema{.fields = record_fields};
constexpr json::Keep records_schema{.element = &record_schema};
constexpr std::array data_fields{
    json::Field{.name = "data", .keep = &records_schema, .streamed = true}};
constexpr std::array prediction_fields{json::Field{
    .name = "predictions", .keep = &records_schema, .streamed = true}};
constexpr json::Keep data_schema{.fields = data_fields};
constexpr json::Keep prediction_schema{.fields = prediction_fields};

// ---- what a body holds
// ---------------------------------------------------------

// A quantity outside the registry: its token and label.
struct Generic {
  std::string_view token;
  std::string_view label;
};

// One column of a body: the record member it comes from, what it is, and the
// unit CO-OPS delivers it in with units=metric.
struct ColumnSpec {
  std::string_view member;
  std::variant<Quantity, Generic> quantity;
  std::string_view unit;
};

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
constexpr std::array salinity_columns{
    ColumnSpec{.member = "s", .quantity = Quantity::salinity, .unit = "PSU"}};
constexpr std::array visibility_columns{
    ColumnSpec{.member = "v", .quantity = Quantity::visibility, .unit = "km"}};
constexpr std::array air_gap_columns{
    ColumnSpec{.member = "v",
               .quantity = Generic{.token = "air_gap", .label = "Air gap"},
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
      break;
  }
  return visibility_columns;
}

// What a body of a series holds: what to keep of it, its columns, and the
// datum of a water level.
struct Layout {
  const json::Keep* keep;
  std::string_view array_key;
  std::span<const ColumnSpec> columns;
  std::optional<core::VerticalDatum> datum;
};

Layout layout_of(const Series& series) {
  return std::visit(
      core::Overloaded{
          [](Observation o) {
            return Layout{.keep = &data_schema,
                          .array_key = "data",
                          .columns = observed_columns,
                          .datum = core::coops::to_vertical(o.datum.get())};
          },
          [](Prediction p) {
            return Layout{.keep = &prediction_schema,
                          .array_key = "predictions",
                          .columns = predicted_columns,
                          .datum = core::coops::to_vertical(p.datum.get())};
          },
          [](core::coops::Meteorological m) {
            return Layout{.keep = &data_schema,
                          .array_key = "data",
                          .columns = met_columns(m.product),
                          .datum = std::nullopt};
          },
          [](core::coops::AirGap) {
            return Layout{.keep = &data_schema,
                          .array_key = "data",
                          .columns = air_gap_columns,
                          .datum = std::nullopt};
          }},
      series);
}

// A failure that reading the pinned tables cannot meet (every spec is read by
// a golden test), reported rather than papered over.
std::unexpected<Error> inconsistent(std::string subject) {
  return io::detail::fail(io::detail::format_error(
      FormatErrc::inconsistent_metadata, std::move(subject)));
}

std::expected<core::QuantityId, Error> quantity_of(const ColumnSpec& spec) {
  return std::visit(
      core::Overloaded{
          [](Quantity q) -> std::expected<core::QuantityId, Error> {
            return core::QuantityId{q};
          },
          [](const Generic& g) -> std::expected<core::QuantityId, Error> {
            auto generic = core::GenericQuantity::parse(
                {.token = g.token, .standard_name = ""});
            if (not generic) {
              return inconsistent(std::string{g.token});
            }
            return core::QuantityId{*std::move(generic)};
          }},
      spec.quantity);
}

std::string label_of(const ColumnSpec& spec) {
  return std::visit(
      core::Overloaded{
          [](Quantity q) { return std::string{core::info(q).long_name}; },
          [](const Generic& g) { return std::string{g.label}; }},
      spec.quantity);
}

// The metadata of a column, with the unit's warning (none for CO-OPS units).
std::expected<Read<core::SeriesMeta>, Error> meta_of(
    const ColumnSpec& spec, std::optional<core::VerticalDatum> datum) {
  auto quantity = quantity_of(spec);
  if (not quantity) {
    return std::unexpected{std::move(quantity).error()};
  }
  Read<std::optional<core::Unit>> unit = io::detail::parsed_unit(spec.unit);
  core::SeriesMeta meta =
      core::SeriesMeta::make({.quantity = *std::move(quantity),
                              .label = label_of(spec),
                              .unit = std::move(unit.value)});
  if (datum) {
    auto with_datum = meta.assume_datum(*datum);
    if (not with_datum) {
      return inconsistent(std::string{spec.member} + ":datum");
    }
    meta = *std::move(with_datum);
  }
  return Read<core::SeriesMeta>{.value = std::move(meta),
                                .warnings = std::move(unit.warnings)};
}

// `t` is exactly "yyyy-MM-dd HH:mm".
constexpr std::size_t time_text_bytes = 16;
constexpr std::size_t time_separator_at = 10;

// One past the opening quote: where a string's text starts.
constexpr std::size_t string_text_offset = 1;

class DataReader {
 public:
  DataReader(std::string_view body, const Series& series,
             const ReadContext& ctx)
      : body_{body},
        layout_{layout_of(series)},
        product_{product_name(series)},
        ctx_{ctx},
        columns_(layout_.columns.size()) {}

  std::expected<Read<Part>, Error> read() && {
    const auto document =
        json::parse(body_, *layout_.keep, ctx_,
                    [this](std::size_t index, const json::Value& record) {
                      return add(index, record);
                    });
    if (not document) {
      return std::unexpected{document.error()};
    }
    const json::Value* array = json::member(document->root, layout_.array_key);
    if (array == nullptr or json::as_array(*array) == nullptr) {
      return io::detail::fail(json::error_at(
          ParseErrc::bad_member, body_,
          array == nullptr ? document->root.offset : array->offset));
    }
    return std::move(*this).columns();
  }

 private:
  [[nodiscard]] ParseError error_at(ParseErrc code, std::size_t offset) const {
    return json::error_at(code, body_, offset);
  }

  // The member `name` of `record` as a string: bad_member at the record when
  // absent, at the value when it is no string.
  [[nodiscard]] std::expected<const json::Value*, Error> text_member(
      const json::Value& record, std::string_view name) const {
    const json::Value* value = json::member(record, name);
    if (value == nullptr) {
      return io::detail::fail(error_at(ParseErrc::bad_member, record.offset));
    }
    if (json::as_string(*value) == nullptr) {
      return io::detail::fail(error_at(ParseErrc::bad_member, value->offset));
    }
    return value;
  }

  std::expected<void, Error> add(std::size_t index, const json::Value& record) {
    const std::size_t samples = (index + 1) * layout_.columns.size();
    if (samples > ctx_.limits.max_elements) {
      return io::detail::fail(ParseError::make(
          ParseErrc::too_large, io::detail::where_of(body_, record.offset),
          std::to_string(samples) + " elements, limit " +
              std::to_string(ctx_.limits.max_elements)));
    }
    if (json::as_object(record) == nullptr) {
      return io::detail::fail(error_at(ParseErrc::bad_member, record.offset));
    }
    const auto time = time_of(record);
    if (not time) {
      return std::unexpected{time.error()};
    }
    // Every value first: a record is taken whole or not at all.
    std::array<core::Sample, 3> values{};
    for (std::size_t c = 0; c < layout_.columns.size(); ++c) {
      const auto sample = sample_of(record, layout_.columns[c].member);
      if (not sample) {
        return std::unexpected{sample.error()};
      }
      values.at(c) = *sample;
    }
    if (auto marks = count_marks(record); not marks) {
      return marks;
    }
    times_.push_back(*time);
    for (std::size_t c = 0; c < layout_.columns.size(); ++c) {
      columns_[c].push_back(values.at(c));
    }
    return {};
  }

  [[nodiscard]] std::expected<core::Time, Error> time_of(
      const json::Value& record) const {
    const auto t = text_member(record, "t");
    if (not t) {
      return std::unexpected{t.error()};
    }
    const std::string& text = *json::as_string(**t);
    const std::size_t start = (*t)->offset + string_text_offset;
    if (text.size() != time_text_bytes) {
      return io::detail::fail(error_at(
          ParseErrc::bad_date, start + std::min(text.size(), time_text_bytes)));
    }
    if (text[time_separator_at] != ' ') {
      return io::detail::fail(
          error_at(ParseErrc::bad_date, start + time_separator_at));
    }
    const auto time = core::parse_utc_datetime(text);
    if (not time) {
      return io::detail::fail(
          error_at(ParseErrc::bad_date, start + time.error().column));
    }
    return *time;
  }

  // A value: "" is Missing; any other text must be one number.
  [[nodiscard]] std::expected<core::Sample, Error> sample_of(
      const json::Value& record, std::string_view member) const {
    const auto value = text_member(record, member);
    if (not value) {
      return std::unexpected{value.error()};
    }
    const std::string& text = *json::as_string(**value);
    if (text.empty()) {
      return core::Sample{core::Missing{}};
    }
    const auto number = io::detail::parse_double(text);
    if (not number) {
      return io::detail::fail(
          error_at(number.error() == io::detail::NumberError::out_of_range
                       ? ParseErrc::out_of_range
                       : ParseErrc::bad_number,
                   (*value)->offset));
    }
    return core::finite_or_missing(*number);
  }

  // An optional string member: absent or null is nullopt.
  [[nodiscard]] std::expected<std::optional<std::string_view>, Error>
  optional_text(const json::Value& record, std::string_view name) const {
    const json::Value* value = json::member(record, name);
    if (value == nullptr or json::is_null(*value)) {
      return std::optional<std::string_view>{};
    }
    const std::string* text = json::as_string(*value);
    if (text == nullptr) {
      return io::detail::fail(error_at(ParseErrc::bad_member, value->offset));
    }
    return std::optional<std::string_view>{*text};
  }

  // A quality flag string ("0,0,0,0") with a nonzero flag in it.
  static bool flagged(std::string_view flags) noexcept {
    return flags.find_first_not_of("0, ") != std::string_view::npos;
  }

  std::expected<void, Error> count_marks(const json::Value& record) {
    const auto flags = optional_text(record, "f");
    if (not flags) {
      return std::unexpected{flags.error()};
    }
    const auto quality = optional_text(record, "q");
    if (not quality) {
      return std::unexpected{quality.error()};
    }
    if (*flags and flagged(**flags)) {
      ++suspect_;
    }
    if (quality->value_or(std::string_view{}) == "p") {
      ++provisional_;
    }
    return {};
  }

  std::expected<Read<Part>, Error> columns() && {
    Read<Part> out{.value = {}, .warnings = {}};
    std::vector<core::Column*> all;
    all.reserve(columns_.size());
    for (core::Column& c : columns_) {
      all.push_back(&c);
    }
    const core::NormalizeReport report =
        io::detail::normalize_columns(times_, all);
    for (std::size_t c = 0; c < layout_.columns.size(); ++c) {
      auto meta = meta_of(layout_.columns[c], layout_.datum);
      if (not meta) {
        return std::unexpected{std::move(meta).error()};
      }
      append(out.warnings, std::move(meta->warnings));
      auto series = core::TimeSeries::make(times_, std::move(columns_[c]),
                                           std::move(meta->value));
      if (not series) {
        return inconsistent(std::string{layout_.columns[c].member} + ":time");
      }
      if (not out.value.insert(*std::move(series))) {
        return inconsistent(std::string{layout_.columns[c].member});
      }
    }
    io::detail::add_normalize_warnings(out.warnings, report,
                                       "coops:" + std::string{product_});
    append_if_counted(out.warnings, {.code = WarningCode::suspect_samples_kept,
                                     .subject = "coops:f",
                                     .count = suspect_});
    append_if_counted(out.warnings, {.code = WarningCode::provisional_samples,
                                     .subject = "coops:q",
                                     .count = provisional_});
    return out;
  }

  std::string_view body_;
  Layout layout_;
  std::string_view product_;
  const ReadContext& ctx_;
  core::TimeAxis times_;
  std::vector<core::Column> columns_;
  std::size_t suspect_ = 0;
  std::size_t provisional_ = 0;
};

}  // namespace

std::expected<DataReply, Error> read_data(std::string_view body,
                                          std::string_view content_type,
                                          const Series& series,
                                          const ReadContext& ctx) {
  return std::visit(
      core::Overloaded{
          [&](Payload) -> std::expected<DataReply, Error> {
            return io::detail::coops::parse_data(body, series, ctx)
                .transform(
                    [](Read<Part> r) { return DataReply{std::move(r)}; });
          },
          [](NoData n) -> std::expected<DataReply, Error> {
            return DataReply{n};
          },
          [](const ServiceError& e) -> std::expected<DataReply, Error> {
            return DataReply{e};
          }},
      classify_body(Api::data, body, content_type));
}

}  // namespace mov::io::wire::coops

namespace mov::io::detail::coops {

const json::Keep& data_keep(const wire::coops::Series& series) {
  return *wire::coops::layout_of(series).keep;
}

std::expected<Read<Part>, Error> parse_data(std::string_view body,
                                            const wire::coops::Series& series,
                                            const ReadContext& ctx) {
  return wire::coops::DataReader{body, series, ctx}.read();
}

}  // namespace mov::io::detail::coops
