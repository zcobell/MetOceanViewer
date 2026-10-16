// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// ndbc::parse_stdmet and read_stdmet: a standard meteorological file to its
// columns (docs/providers-design.md section 4.4).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/ascii.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/overloaded.hpp"
#include "mov/core/provider/ndbc.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"
#include "mov/io/detail/civil_time.hpp"
#include "mov/io/detail/line_cursor.hpp"
#include "mov/io/detail/normalize.hpp"
#include "mov/io/detail/parse_at.hpp"
#include "mov/io/detail/reporting.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/error.hpp"
#include "mov/io/gzip.hpp"
#include "mov/io/part.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/body.hpp"
#include "mov/io/wire/ndbc.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/io/wire/service.hpp"

namespace mov::io::wire::ndbc {

namespace {

using core::Quantity;
using core::ndbc::FileKind;
using Line = io::detail::LineCursor::Line;

// ---- the columns
// ------------------------------------------------------------------

// What a column's unit must measure.
enum class Family : std::uint8_t {
  direction,
  speed,
  length,
  period,
  pressure,
  temperature
};

// A quantity outside the registry: its token and label.
struct Generic {
  std::string_view token;
  std::string_view label;
};

struct ColumnSpec {
  std::string_view token;  // the name NDBC writes now
  std::string_view alias;  // its name before 2007, or empty
  std::variant<Quantity, Generic> quantity;
  Family family;
  // The unit without a units row: in a historical file, and in any other.
  std::string_view historical_unit;
  std::string_view other_unit;
  // The value a historical file writes for missing (a run of 9s).
  std::optional<double> missing;
  std::optional<core::VerticalDatum> datum;
};

constexpr double nines_2 = 99.0;
constexpr double nines_3 = 999.0;
constexpr double nines_4 = 9999.0;

// NDBC's measurement descriptions (https://www.ndbc.noaa.gov/faq/measdes.shtml,
// read 2026-10-15): TIDE is feet above or below MLLW; VIS nautical miles in
// the realtime files. The missing values are the documented runs of 9s, as
// the recorded files write them.
constexpr std::array<ColumnSpec, 14> column_specs{{
    {.token = "WDIR",
     .alias = "WD",
     .quantity = Quantity::wind_direction,
     .family = Family::direction,
     .historical_unit = "degT",
     .other_unit = "degT",
     .missing = nines_3,
     .datum = std::nullopt},
    {.token = "WSPD",
     .alias = "",
     .quantity = Quantity::wind_speed,
     .family = Family::speed,
     .historical_unit = "m/s",
     .other_unit = "m/s",
     .missing = nines_2,
     .datum = std::nullopt},
    {.token = "GST",
     .alias = "",
     .quantity = Quantity::wind_gust,
     .family = Family::speed,
     .historical_unit = "m/s",
     .other_unit = "m/s",
     .missing = nines_2,
     .datum = std::nullopt},
    {.token = "WVHT",
     .alias = "",
     .quantity = Quantity::wave_height,
     .family = Family::length,
     .historical_unit = "m",
     .other_unit = "m",
     .missing = nines_2,
     .datum = std::nullopt},
    {.token = "DPD",
     .alias = "",
     .quantity = Quantity::wave_period_dominant,
     .family = Family::period,
     .historical_unit = "sec",
     .other_unit = "sec",
     .missing = nines_2,
     .datum = std::nullopt},
    {.token = "APD",
     .alias = "",
     .quantity = Quantity::wave_period_average,
     .family = Family::period,
     .historical_unit = "sec",
     .other_unit = "sec",
     .missing = nines_2,
     .datum = std::nullopt},
    {.token = "MWD",
     .alias = "",
     .quantity = Quantity::wave_direction,
     .family = Family::direction,
     .historical_unit = "degT",
     .other_unit = "degT",
     .missing = nines_3,
     .datum = std::nullopt},
    {.token = "PRES",
     .alias = "BAR",
     .quantity = Quantity::air_pressure,
     .family = Family::pressure,
     .historical_unit = "hPa",
     .other_unit = "hPa",
     .missing = nines_4,
     .datum = std::nullopt},
    {.token = "ATMP",
     .alias = "",
     .quantity = Quantity::air_temperature,
     .family = Family::temperature,
     .historical_unit = "degC",
     .other_unit = "degC",
     .missing = nines_3,
     .datum = std::nullopt},
    {.token = "WTMP",
     .alias = "",
     .quantity = Quantity::water_temperature,
     .family = Family::temperature,
     .historical_unit = "degC",
     .other_unit = "degC",
     .missing = nines_3,
     .datum = std::nullopt},
    {.token = "DEWP",
     .alias = "",
     .quantity = Quantity::dew_point,
     .family = Family::temperature,
     .historical_unit = "degC",
     .other_unit = "degC",
     .missing = nines_3,
     .datum = std::nullopt},
    {.token = "VIS",
     .alias = "",
     .quantity = Quantity::visibility,
     .family = Family::length,
     .historical_unit = "mi",
     .other_unit = "nmi",
     .missing = nines_2,
     .datum = std::nullopt},
    {.token = "PTDY",
     .alias = "",
     .quantity =
         Generic{.token = "pressure_tendency", .label = "Pressure tendency"},
     .family = Family::pressure,
     .historical_unit = "hPa",
     .other_unit = "hPa",
     .missing = std::nullopt,
     .datum = std::nullopt},
    {.token = "TIDE",
     .alias = "",
     .quantity = Quantity::water_level,
     .family = Family::length,
     .historical_unit = "ft",
     .other_unit = "ft",
     .missing = nines_2,
     .datum = core::VerticalDatum::mllw},
}};

[[nodiscard]] const ColumnSpec* spec_named(std::string_view token) noexcept {
  const auto found = std::ranges::find_if(column_specs, [&](const auto& s) {
    return s.token == token or s.alias == token;  // a word is never empty
  });
  return found == column_specs.end() ? nullptr : &*found;
}

[[nodiscard]] std::string_view kind_token(FileKind kind) noexcept {
  switch (kind) {
    case FileKind::historical:
      return "historical";
    case FileKind::monthly:
      return "monthly";
    case FileKind::monthly_latest:
      return "monthly_latest";
    case FileKind::realtime:
      break;
  }
  return "realtime";
}

[[nodiscard]] bool of_family(const core::Unit& unit, Family family) {
  switch (family) {
    case Family::direction:
      return unit == core::degree();
    case Family::speed:
      return std::holds_alternative<core::SpeedUnit>(unit);
    case Family::length:
      return std::holds_alternative<core::LengthUnit>(unit);
    case Family::pressure:
      return std::holds_alternative<core::PressureUnit>(unit);
    case Family::temperature:
      return std::holds_alternative<core::TemperatureUnit>(unit);
    case Family::period:
      break;
  }
  const auto* other = std::get_if<core::OtherUnit>(&unit);
  return other != nullptr and other->symbol() == "s";
}

[[nodiscard]] std::unexpected<Error> inconsistent(std::string subject) {
  return io::detail::fail(io::detail::format_error(
      FormatErrc::inconsistent_metadata, std::move(subject)));
}

// ---- the header
// -------------------------------------------------------------------

// A recognized column of the file: what it is, where its value is in a row,
// its name as written and its unit.
struct HeaderColumn {
  const ColumnSpec* spec;
  std::size_t field;
  std::string_view written;
  std::optional<core::Unit> unit;
};

struct Header {
  std::size_t fields = 0;  // words per row
  bool minutes = false;
  std::vector<HeaderColumn> columns;
  std::vector<Warning> warnings;
};

constexpr std::array<std::string_view, 5> time_names{"YY", "MM", "DD", "hh",
                                                     "mm"};
constexpr std::array<std::string_view, 5> time_units{"#yr", "mo", "dy", "hr",
                                                     "mn"};
constexpr std::size_t date_fields = 4;  // year, month, day, hour

[[nodiscard]] ParseError missing_header(const Line& line,
                                        std::string_view token) {
  return io::detail::at(line, token, ParseErrc::missing_header);
}

// Whether the header's i-th word is the i-th time column ("YY" also as
// "#YY", "YYYY" or "#YYYY").
[[nodiscard]] bool is_time_name(std::string_view word, std::size_t i) {
  if (i == 0) {
    if (word.starts_with('#')) {
      word.remove_prefix(1);
    }
    return word == "YY" or word == "YYYY";
  }
  return word == time_names.at(i);
}

class HeaderReader {
 public:
  HeaderReader(io::detail::LineCursor& cursor, FileKind kind)
      : cursor_{cursor}, kind_{kind} {}

  std::expected<Header, Error> read() && {
    const auto first = cursor_.next_nonblank();
    if (not first) {
      return io::detail::fail(
          ParseError::make(ParseErrc::empty_input, {.line = 1}, ""));
    }
    const std::vector<std::string_view> words =
        io::detail::split_ws(first->text);
    if (auto times = time_columns(*first, words); not times) {
      return std::unexpected{std::move(times).error()};
    }
    header_.fields = words.size();
    if (auto columns = data_columns(words); not columns) {
      return std::unexpected{std::move(columns).error()};
    }
    if (auto units = units_row(); not units) {
      return std::unexpected{std::move(units).error()};
    }
    return std::move(header_);
  }

 private:
  std::expected<void, Error> time_columns(
      const Line& line, const std::vector<std::string_view>& words) {
    for (std::size_t i = 0; i < date_fields; ++i) {
      if (i >= words.size() or not is_time_name(words[i], i)) {
        return io::detail::fail(missing_header(
            line, i < words.size() ? words[i] : std::string_view{}));
      }
    }
    header_.minutes = words.size() > date_fields and
                      words[date_fields] == time_names.at(date_fields);
    return {};
  }

  std::expected<void, Error> data_columns(
      const std::vector<std::string_view>& words) {
    for (std::size_t i = time_fields(); i < words.size(); ++i) {
      const ColumnSpec* spec = spec_named(words[i]);
      if (spec == nullptr) {
        header_.warnings.push_back({.code = WarningCode::skipped_variable,
                                    .subject = io::detail::subject_of(words[i]),
                                    .count = 1});
        continue;
      }
      if (std::ranges::any_of(header_.columns, [&](const HeaderColumn& c) {
            return c.spec == spec;
          })) {
        return io::detail::fail(io::detail::format_error(
            FormatErrc::duplicate_quantity, std::string{words[i]}));
      }
      header_.columns.push_back(
          {.spec = spec, .field = i, .written = words[i], .unit = {}});
    }
    return {};
  }

  [[nodiscard]] std::size_t time_fields() const noexcept {
    return header_.minutes ? date_fields + 1 : date_fields;
  }

  // The units row, if the next line is one, else the documented defaults.
  std::expected<void, Error> units_row() {
    const auto next = cursor_.peek_nonblank();
    if (not next or not io::detail::skip_space(next->text).starts_with('#')) {
      return assign_units({});
    }
    static_cast<void>(cursor_.next_nonblank());  // the line peeked
    const Line& line = *next;
    const std::vector<std::string_view> words = io::detail::split_ws(line.text);
    if (words.size() != header_.fields) {
      return io::detail::fail(ParseError::make(
          ParseErrc::wrong_field_count, {.line = line.number}, line.text));
    }
    for (std::size_t i = 0; i < time_fields(); ++i) {
      if (words[i] != time_units.at(i)) {
        return io::detail::fail(missing_header(line, words[i]));
      }
    }
    return assign_units(words);
  }

  std::expected<void, Error> assign_units(
      const std::vector<std::string_view>& row) {
    for (HeaderColumn& c : header_.columns) {
      const std::string_view text =
          row.empty() ? default_unit(*c.spec) : row[c.field];
      Read<std::optional<core::Unit>> unit = io::detail::parsed_unit(text);
      if (not unit.value or not of_family(*unit.value, c.spec->family)) {
        return io::detail::fail(io::detail::format_error(
            FormatErrc::noncanonical_unit,
            io::detail::subject_of(std::string{c.written} + " " +
                                   std::string{text})));
      }
      append(header_.warnings, std::move(unit.warnings));
      c.unit = std::move(unit.value);
    }
    return {};
  }

  // The unit of a column in a file without a units row.
  [[nodiscard]] std::string_view default_unit(
      const ColumnSpec& spec) const noexcept {
    return kind_ == FileKind::historical ? spec.historical_unit
                                         : spec.other_unit;
  }

  io::detail::LineCursor& cursor_;
  FileKind kind_;
  Header header_;
};

// ---- the rows
// ---------------------------------------------------------------------

constexpr unsigned last_month = 12;
constexpr unsigned last_hour = 23;
constexpr unsigned last_minute = 59;
constexpr std::int64_t two_digit_century = 1900;
constexpr std::size_t two_digit_year = 2;
constexpr std::size_t four_digit_year = 4;
constexpr std::size_t max_field_digits = 2;
constexpr std::size_t stop_poll_rows = 4096;

[[nodiscard]] bool all_digits(std::string_view token) noexcept {
  return not token.empty() and
         std::ranges::all_of(token, core::ascii::is_digit);
}

// A month, day, hour or minute: one or two digits within [lo, hi].
[[nodiscard]] std::expected<unsigned, ParseError> time_field(
    const Line& line, std::string_view token, unsigned lo, unsigned hi) {
  if (token.size() > max_field_digits or not all_digits(token)) {
    return std::unexpected{io::detail::at(line, token, ParseErrc::bad_date)};
  }
  const auto value = io::detail::parse_int<unsigned>(token);
  if (not value or *value < lo or *value > hi) {
    return std::unexpected{io::detail::at(line, token, ParseErrc::bad_date)};
  }
  return *value;
}

// Two digits are 19YY (the files before 1999), four are the year.
[[nodiscard]] std::expected<std::int64_t, ParseError> year_field(
    const Line& line, std::string_view token) {
  if ((token.size() != two_digit_year and token.size() != four_digit_year) or
      not all_digits(token)) {
    return std::unexpected{io::detail::at(line, token, ParseErrc::bad_date)};
  }
  const auto value = io::detail::parse_int<std::int64_t>(token);
  if (not value) {
    return std::unexpected{io::detail::at(line, token, ParseErrc::bad_date)};
  }
  return token.size() == two_digit_year ? two_digit_century + *value : *value;
}

[[nodiscard]] std::expected<core::Time, ParseError> time_of(
    const Line& line, std::span<const std::string_view> words, bool minutes) {
  const auto year = year_field(line, words[0]);
  if (not year) {
    return std::unexpected{year.error()};
  }
  const auto month = time_field(line, words[1], 1, last_month);
  if (not month) {
    return std::unexpected{month.error()};
  }
  const auto day =
      time_field(line, words[2], 1, io::detail::days_in_month(*year, *month));
  if (not day) {
    return std::unexpected{day.error()};
  }
  const auto hour = time_field(line, words[3], 0, last_hour);
  if (not hour) {
    return std::unexpected{hour.error()};
  }
  const auto minute = minutes
                          ? time_field(line, words[date_fields], 0, last_minute)
                          : std::expected<unsigned, ParseError>{0U};
  if (not minute) {
    return std::unexpected{minute.error()};
  }
  return io::detail::time_of({.year = *year,
                              .month = *month,
                              .day = *day,
                              .hour = *hour,
                              .minute = *minute,
                              .second = 0,
                              .millisecond = 0});
}

// The metadata of a column.
[[nodiscard]] std::expected<core::SeriesMeta, Error> meta_of(
    const HeaderColumn& column) {
  const ColumnSpec& spec = *column.spec;
  auto quantity = std::visit(
      core::Overloaded{
          [](Quantity q) -> std::optional<core::QuantityId> {
            return core::QuantityId{q};
          },
          [](const Generic& g) -> std::optional<core::QuantityId> {
            return core::GenericQuantity::parse(
                       {.token = g.token, .standard_name = ""})
                .transform([](core::GenericQuantity q) -> core::QuantityId {
                  return q;
                });
          }},
      spec.quantity);
  if (not quantity) {
    return inconsistent(std::string{spec.token});
  }
  const std::string label = std::visit(
      core::Overloaded{
          [](Quantity q) { return std::string{core::info(q).long_name}; },
          [](const Generic& g) { return std::string{g.label}; }},
      spec.quantity);
  core::SeriesMeta meta = core::SeriesMeta::make(
      {.quantity = *std::move(quantity), .label = label, .unit = column.unit});
  if (not spec.datum) {
    return meta;
  }
  return meta.assume_datum(*spec.datum)
      .transform_error([&](core::AssumeDatumError) -> Error {
        return io::detail::format_error(FormatErrc::inconsistent_metadata,
                                        std::string{spec.token} + ":datum");
      });
}

// A value of a row, and whether it was the column's 9-filled missing value.
struct Value {
  core::Sample sample;
  bool masked = false;
};

// `MM` in any column, the column's 9-filled value; otherwise a number.
[[nodiscard]] std::expected<Value, ParseError> value_of(
    const Line& line, std::string_view token, const ColumnSpec& spec) {
  if (token == "MM") {
    return Value{.sample = core::Missing{}, .masked = false};
  }
  const auto number = io::detail::double_at(line, token);
  if (not number) {
    return std::unexpected{number.error()};
  }
  if (spec.missing == *number) {
    return Value{.sample = core::Missing{}, .masked = true};
  }
  return Value{.sample = core::finite_or_missing(*number), .masked = false};
}

class StdMetReader {
 public:
  StdMetReader(std::string_view text, FileKind kind, const ReadContext& ctx)
      : cursor_{text}, kind_{kind}, ctx_{ctx} {}

  std::expected<Read<Part>, Error> read() && {
    auto header = HeaderReader{cursor_, kind_}.read();
    if (not header) {
      return std::unexpected{std::move(header).error()};
    }
    header_ = *std::move(header);
    columns_.resize(header_.columns.size());
    masked_.resize(header_.columns.size());
    words_.resize(header_.fields);
    while (const auto line = cursor_.next_nonblank()) {
      if (rows_ % stop_poll_rows == 0 and ctx_.stop.stop_requested()) {
        return std::unexpected{Error{Cancelled{}}};
      }
      if (auto added = add(*line); not added) {
        return std::unexpected{std::move(added).error()};
      }
    }
    return std::move(*this).part();
  }

 private:
  std::expected<void, Error> add(const Line& line) {
    if (io::detail::skip_space(line.text).starts_with('#')) {
      if (comments_ == 0) {
        first_comment_ = io::detail::subject_of(line.text);
      }
      ++comments_;
      return {};
    }
    if (io::detail::split_ws_into(line.text, words_) != header_.fields) {
      return io::detail::fail(ParseError::make(
          ParseErrc::wrong_field_count, {.line = line.number}, line.text));
    }
    const std::size_t width = std::max<std::size_t>(columns_.size(), 1);
    if ((rows_ + 1) > ctx_.limits.max_elements / width) {
      return io::detail::fail(ParseError::make(
          ParseErrc::too_large, {.line = line.number},
          std::to_string(rows_ + 1) + " rows of " + std::to_string(width) +
              " columns, limit " + std::to_string(ctx_.limits.max_elements) +
              " elements"));
    }
    const auto time = time_of(line, words_, header_.minutes);
    if (not time) {
      return io::detail::fail(time.error());
    }
    // Every value first: a row is taken whole or not at all.
    row_.clear();
    for (const HeaderColumn& c : header_.columns) {
      const auto value = value_of(line, words_[c.field], *c.spec);
      if (not value) {
        return io::detail::fail(value.error());
      }
      row_.push_back(*value);
    }
    times_.push_back(*time);
    for (std::size_t c = 0; c < row_.size(); ++c) {
      columns_[c].push_back(row_[c].sample);
      masked_[c] += row_[c].masked ? 1U : 0U;
    }
    ++rows_;
    return {};
  }

  // Realtime files are newest first: reversed, not reported. Anything else
  // out of order is normalize's.
  core::NormalizeReport put_in_order() {
    const bool decreasing =
        times_.size() > 1 and
        std::ranges::adjacent_find(times_, std::less_equal{}) == times_.end();
    if (decreasing) {
      std::ranges::reverse(times_);
      for (core::Column& c : columns_) {
        std::ranges::reverse(c);
      }
      return {};
    }
    std::vector<core::Column*> all;
    all.reserve(columns_.size());
    for (core::Column& c : columns_) {
      all.push_back(&c);
    }
    return io::detail::normalize_columns(times_, all);
  }

  std::expected<Read<Part>, Error> part() && {
    const core::NormalizeReport report = put_in_order();
    Read<Part> out{.value = {}, .warnings = std::move(header_.warnings)};
    for (std::size_t c = 0; c < header_.columns.size(); ++c) {
      const HeaderColumn& column = header_.columns[c];
      auto meta = meta_of(column);
      if (not meta) {
        return std::unexpected{std::move(meta).error()};
      }
      auto series = core::TimeSeries::make(times_, std::move(columns_[c]),
                                           *std::move(meta));
      if (not series) {
        return inconsistent(std::string{column.written} + ":time");
      }
      if (not out.value.insert(*std::move(series))) {
        return inconsistent(std::string{column.written});
      }
      append_if_counted(out.warnings,
                        {.code = WarningCode::legacy_sentinel_masked,
                         .subject = "ndbc:" + std::string{column.written},
                         .count = masked_[c]});
    }
    append_if_counted(out.warnings, {.code = WarningCode::header_line_skipped,
                                     .subject = std::move(first_comment_),
                                     .count = comments_});
    io::detail::add_normalize_warnings(
        out.warnings, report, "ndbc:" + std::string{kind_token(kind_)});
    return out;
  }

  io::detail::LineCursor cursor_;
  FileKind kind_;
  const ReadContext& ctx_;
  Header header_;
  std::vector<std::string_view> words_;
  std::vector<Value> row_;
  core::TimeAxis times_;
  std::vector<core::Column> columns_;
  std::vector<std::size_t> masked_;
  std::size_t rows_ = 0;
  std::size_t comments_ = 0;
  std::string first_comment_;
};

[[nodiscard]] ParseError too_large(std::uintmax_t bytes, std::uintmax_t limit) {
  return ParseError::make(
      ParseErrc::too_large, {.line = 1},
      std::to_string(bytes) + " bytes, limit " + std::to_string(limit));
}

[[nodiscard]] std::string_view gzip_reason(GzipErrc code) noexcept {
  switch (code) {
    case GzipErrc::not_gzip:
      return "not gzip";
    case GzipErrc::corrupt:
      return "corrupt gzip stream";
    case GzipErrc::truncated:
      return "truncated gzip stream";
    case GzipErrc::extra_member:
      return "a second gzip member";
    case GzipErrc::trailing_bytes:
      return "bytes after the gzip member";
    case GzipErrc::too_large:
      break;
  }
  return "inflates past the limit";
}

// A gunzip failure as an io::Error: too_large for the cap, else bad_gzip at
// the byte offset.
[[nodiscard]] Error lift_gunzip(const GunzipError& e) {
  return std::visit(
      core::Overloaded{
          [](core::Cancelled c) -> Error { return c; },
          [](const GzipError& g) -> Error {
            return ParseError::make(
                g.code == GzipErrc::too_large ? ParseErrc::too_large
                                              : ParseErrc::bad_gzip,
                {.line = 1, .column = static_cast<std::size_t>(g.offset)},
                gzip_reason(g.code));
          }},
      e);
}

// The body as text: inflated when it is gzip, copied when it is not.
[[nodiscard]] std::expected<std::string, Error> body_text(
    std::span<const std::byte> body, const ReadContext& ctx) {
  if (has_gzip_magic(body)) {
    return gunzip(body, ctx.limits.max_text_bytes, ctx.stop)
        .transform_error(lift_gunzip);
  }
  if (body.size() > ctx.limits.max_text_bytes) {
    return io::detail::fail(too_large(body.size(), ctx.limits.max_text_bytes));
  }
  std::string text(body.size(), '\0');
  std::ranges::transform(body, text.begin(),
                         [](std::byte b) { return static_cast<char>(b); });
  return text;
}

}  // namespace

std::expected<Read<Part>, Error> parse_stdmet(std::string_view text,
                                              FileKind kind,
                                              const ReadContext& ctx) {
  if (text.size() > ctx.limits.max_text_bytes) {
    return io::detail::fail(too_large(text.size(), ctx.limits.max_text_bytes));
  }
  if (ctx.stop.stop_requested()) {
    return std::unexpected{Error{Cancelled{}}};
  }
  return StdMetReader{text, kind, ctx}.read();
}

std::expected<StdMetReply, Error> read_stdmet(std::span<const std::byte> body,
                                              std::string_view content_type,
                                              FileKind kind,
                                              const ReadContext& ctx) {
  const auto text = body_text(body, ctx);
  if (not text) {
    return std::unexpected{text.error()};
  }
  if (auto markup = sniff_markup(Origin::ndbc, *text, content_type)) {
    return StdMetReply{*std::move(markup)};
  }
  return parse_stdmet(*text, kind, ctx).transform([](Read<Part> r) {
    return StdMetReply{std::move(r)};
  });
}

}  // namespace mov::io::wire::ndbc
