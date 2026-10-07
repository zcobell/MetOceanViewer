// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/imeds.hpp"

#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/detail/ascii.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/io/detail/line_cursor.hpp"
#include "mov/io/detail/parse_at.hpp"
#include "mov/io/detail/station_names.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/text_file.hpp"

namespace mov::io {

namespace {

namespace chrono = std::chrono;
using detail::LineCursor;

// A data row has at most 7 words and a station line 3; one slot more makes
// split_ws_into report a longer line.
constexpr std::size_t word_slots = 8;
constexpr std::size_t header_lines = 3;
// How often read_imeds asks whether it should stop.
constexpr std::size_t stop_poll_interval = 4096;

// ---- header
// ------------------------------------------------------------------

bool is_utc_token(std::string_view zone) {
  return core::detail::equal_ignore_case(zone, "UTC") or
         core::detail::equal_ignore_case(zone, "GMT") or
         core::detail::equal_ignore_case(zone, "Z");
}

struct HeaderRead {
  ImedsHeader header;
  std::vector<Warning> warnings;
};

// The unit is the rest of the line after the datum word, so "S m-1" is one.
std::optional<core::Unit> header_unit(std::string_view text,
                                      std::vector<Warning>& warnings) {
  const std::string_view trimmed = core::detail::trim(text);
  if (core::detail::equal_ignore_case(trimmed, "unknown")) {
    return std::nullopt;
  }
  std::optional<core::Unit> unit = core::parse_unit(trimmed);
  const auto* other = unit ? std::get_if<core::OtherUnit>(&*unit) : nullptr;
  if (other != nullptr and not core::is_canonical_other(*other)) {
    warnings.push_back({.code = WarningCode::unrecognized_unit,
                        .subject = std::string{trimmed}});
  }
  return unit;
}

std::optional<core::VerticalDatum> header_datum(
    std::string_view token, std::vector<Warning>& warnings) {
  const auto datum = core::parse_vertical_datum(token);
  if (datum) {
    return *datum;
  }
  warnings.push_back(
      {.code = WarningCode::datum_unknown, .subject = std::string{token}});
  return std::nullopt;
}

// Line 3: <source> [<time zone> [<datum> [<unit>]]]. Only the source is
// required.
std::expected<HeaderRead, ParseError> parse_header(
    const LineCursor::Line& line) {
  const std::vector<std::string_view> words = detail::split_ws(line.text);
  if (words.empty()) {
    return std::unexpected{ParseError::make(ParseErrc::missing_header,
                                            {.line = line.number}, line.text)};
  }
  HeaderRead read{.header = {.source = std::string{words[0]},
                             .time_zone = {},
                             .datum = std::nullopt,
                             .unit = std::nullopt},
                  .warnings = {}};
  if (words.size() > 1) {
    read.header.time_zone = std::string{words[1]};
  }
  if (words.size() < 2 or not is_utc_token(words[1])) {
    read.warnings.push_back({.code = WarningCode::tz_assumed_utc,
                             .subject = read.header.time_zone});
  }
  if (words.size() > 2) {
    read.header.datum = header_datum(words[2], read.warnings);
  }
  if (words.size() > 3) {
    const auto offset =
        static_cast<std::size_t>(words[3].data() - line.text.data());
    read.header.unit = header_unit(line.text.substr(offset), read.warnings);
  }
  return read;
}

core::SeriesMeta column_meta(const ImedsHeader& header) {
  const core::SeriesMeta meta = core::SeriesMeta::make({.unit = header.unit});
  // The generic quantity can always carry a datum, so assume_datum never
  // falls back.
  return header.datum ? meta.assume_datum(*header.datum).value_or(meta) : meta;
}

// ---- values
// ------------------------------------------------------------------

// v4 printed its null, -DBL_MAX, as "%10.4e" (N18).
constexpr std::string_view printed_dbl_max{"-1.7977e+308"};

constexpr std::array<double, 3> legacy_sentinels{
    -99999.0, -9999.0, -std::numeric_limits<double>::max()};

bool is_legacy_sentinel(double value) {
  return std::ranges::any_of(legacy_sentinels, [value](double sentinel) {
    return std::bit_cast<std::uint64_t>(sentinel) ==
           std::bit_cast<std::uint64_t>(value);
  });
}

struct RowValue {
  core::Sample sample;
  bool masked;
};

std::expected<RowValue, ParseError> parse_value(const LineCursor::Line& line,
                                                std::string_view token) {
  if (core::detail::equal_ignore_case(token, printed_dbl_max)) {
    return RowValue{.sample = core::Missing{}, .masked = true};
  }
  const auto number = detail::double_at(line, token);
  if (not number) {
    return std::unexpected{number.error()};
  }
  if (is_legacy_sentinel(*number)) {
    return RowValue{.sample = core::Missing{}, .masked = true};
  }
  // parse_double yields only finite numbers: this is Sample::of, made total.
  return RowValue{.sample = core::finite_or_missing(*number), .masked = false};
}

// ---- times
// -------------------------------------------------------------------

struct DateFields {
  int year, month, day, hour, minute, second;
};

constexpr int max_year = 9999;

// The date of fields whose month and day are in 1..12 and 1..31 (the
// narrowing below is then exact).
chrono::year_month_day date_of(const DateFields& f) {
  return {chrono::year{f.year}, chrono::month{static_cast<unsigned>(f.month)},
          chrono::day{static_cast<unsigned>(f.day)}};
}

// The index of the first field (in DateFields order) that is not a real
// date or time of day, or nullopt.
std::optional<std::size_t> first_bad_field(const DateFields& f) {
  if (f.year < 0 or f.year > max_year) {
    return 0;
  }
  if (f.month < 1 or f.month > 12) {
    return 1;
  }
  if (f.day < 1 or f.day > 31 or not date_of(f).ok()) {
    return 2;
  }
  if (f.hour < 0 or f.hour > 23) {
    return 3;
  }
  if (f.minute < 0 or f.minute > 59) {
    return 4;
  }
  if (f.second < 0 or f.second > 59) {
    return 5;
  }
  return std::nullopt;
}

// Fields that first_bad_field accepted.
core::Time time_of(const DateFields& f) {
  return chrono::time_point_cast<chrono::milliseconds>(
             chrono::sys_days{date_of(f)}) +
         chrono::hours{f.hour} + chrono::minutes{f.minute} +
         chrono::seconds{f.second};
}

// The integers of a row: five (no seconds) or six words.
std::expected<core::Time, ParseError> parse_time(
    const LineCursor::Line& line, std::span<const std::string_view> words) {
  std::array<int, 6> numbers{};  // seconds stay 0 for a 6-word row
  for (const std::size_t i : std::views::iota(std::size_t{0}, words.size())) {
    const auto number = detail::int_at<int>(line, words[i]);
    if (not number) {
      return std::unexpected{number.error()};
    }
    numbers[i] = *number;
  }
  const DateFields fields{.year = numbers[0],
                          .month = numbers[1],
                          .day = numbers[2],
                          .hour = numbers[3],
                          .minute = numbers[4],
                          .second = numbers[5]};
  if (const auto bad = first_bad_field(fields)) {
    return std::unexpected{detail::at(line, words[*bad], ParseErrc::bad_date)};
  }
  return time_of(fields);
}

// ---- stations
// ----------------------------------------------------------------

struct StationBlock {
  std::string name;
  core::Location location;
  std::vector<core::Point> rows;
};

std::expected<core::Location, ParseError> parse_location(
    const LineCursor::Line& line, std::string_view lat_token,
    std::string_view lon_token) {
  const auto lat = detail::double_at(line, lat_token);
  if (not lat) {
    return std::unexpected{lat.error()};
  }
  const auto lon = detail::double_at(line, lon_token);
  if (not lon) {
    return std::unexpected{lon.error()};
  }
  const auto location = core::Location::make({.lat = *lat, .lon = *lon});
  if (not location) {
    const bool bad_longitude =
        location.error() == core::LocationError::longitude_out_of_range;
    return std::unexpected{detail::at(
        line, bad_longitude ? lon_token : lat_token, ParseErrc::out_of_range)};
  }
  return *location;
}

// ---- the parser
// --------------------------------------------------------------

class ImedsParser {
 public:
  // One line at a time, so that read_imeds can stop between lines.
  [[nodiscard]] std::expected<void, ParseError> feed(
      const LineCursor::Line& line);

  [[nodiscard]] std::size_t samples() const noexcept { return samples_; }

  [[nodiscard]] std::expected<Read<ImedsFile>, ParseError> finish() &&;

 private:
  [[nodiscard]] std::expected<void, ParseError> take_header(
      const LineCursor::Line& line);
  [[nodiscard]] std::expected<void, ParseError> begin_station(
      const LineCursor::Line& line,
      const std::array<std::string_view, word_slots>& words);
  [[nodiscard]] std::expected<void, ParseError> add_row(
      const LineCursor::Line& line,
      const std::array<std::string_view, word_slots>& words, std::size_t count);

  std::size_t lines_{0};
  std::optional<HeaderRead> header_;
  std::vector<StationBlock> stations_;
  std::size_t masked_{0};
  std::size_t renamed_names_{0};  // names with bytes replaced
  std::size_t samples_{0};
};

std::expected<void, ParseError> ImedsParser::take_header(
    const LineCursor::Line& line) {
  ++lines_;
  if (lines_ < header_lines) {
    return {};  // lines 1 and 2 are free text
  }
  auto parsed = parse_header(line);
  if (not parsed) {
    return std::unexpected{std::move(parsed).error()};
  }
  header_ = *std::move(parsed);
  return {};
}

std::expected<void, ParseError> ImedsParser::begin_station(
    const LineCursor::Line& line,
    const std::array<std::string_view, word_slots>& words) {
  const auto location = parse_location(line, words[1], words[2]);
  if (not location) {
    return std::unexpected{location.error()};
  }
  detail::CleanedText name = detail::replace_invalid_utf8(words[0]);
  if (name.replaced) {
    ++renamed_names_;
  }
  stations_.push_back(
      {.name = std::move(name.text), .location = *location, .rows = {}});
  return {};
}

std::expected<void, ParseError> ImedsParser::add_row(
    const LineCursor::Line& line,
    const std::array<std::string_view, word_slots>& words, std::size_t count) {
  if (stations_.empty()) {
    return std::unexpected{
        detail::at(line, words[0], ParseErrc::missing_header)};
  }
  const auto time = parse_time(line, std::span{words}.first(count - 1));
  if (not time) {
    return std::unexpected{time.error()};
  }
  const auto value = parse_value(line, words[count - 1]);
  if (not value) {
    return std::unexpected{value.error()};
  }
  masked_ += value->masked ? 1U : 0U;
  ++samples_;
  stations_.back().rows.push_back({.time = *time, .sample = value->sample});
  return {};
}

std::expected<void, ParseError> ImedsParser::feed(
    const LineCursor::Line& line) {
  if (lines_ < header_lines) {
    return take_header(line);
  }
  std::array<std::string_view, word_slots> words{};
  const std::size_t count = detail::split_ws_into(line.text, words);
  switch (count) {
    case 0:
      return {};  // blank line (N13)
    case 3:
      return begin_station(line, words);
    case 6:
    case 7:
      return add_row(line, words, count);
    default:
      return std::unexpected{
          detail::at(line, words[0], ParseErrc::wrong_field_count)};
  }
}

// What finish() builds: the table and the warnings in the order of Read's
// documentation.
struct Assembled {
  core::StationTable table;
  std::vector<Warning> warnings;
};

void add_report_warnings(const core::NormalizeReport& report,
                         const std::string& id,
                         std::vector<Warning>& warnings) {
  const auto add = [&](WarningCode code, std::size_t count) {
    if (count > 0) {
      warnings.push_back({.code = code, .subject = id, .count = count});
    }
  };
  add(WarningCode::times_reordered, report.descents);
  add(WarningCode::duplicate_times_dropped, report.duplicates_dropped);
  add(WarningCode::conflicting_duplicate_times, report.conflicting_duplicates);
}

ParseError internal_error(std::string_view what) {
  return ParseError::make(ParseErrc::corrupt_record, {.line = 0}, what);
}

std::expected<Assembled, ParseError> assemble(std::vector<StationBlock> blocks,
                                              const core::SeriesMeta& meta,
                                              const detail::UniqueIds& unique) {
  std::vector<core::TimeAxis> axes;
  std::vector<core::Column> column;
  std::vector<core::StationRow> rows;
  std::vector<Warning> warnings;
  for (const std::size_t i : std::views::iota(std::size_t{0}, blocks.size())) {
    StationBlock& block = blocks[i];
    auto key = core::StationKey::make(unique.ids[i]);
    auto name = core::StationText::make(block.name);
    if (not key or not name) {  // the names were cleaned: not reachable
      return std::unexpected{internal_error("station name")};
    }
    core::Normalized normalized = core::normalize(std::move(block.rows), meta);
    add_report_warnings(normalized.report, unique.ids[i], warnings);
    core::TimeSeriesParts parts = std::move(normalized.series).into_parts();
    axes.push_back(std::move(parts.times));
    column.push_back(std::move(parts.samples));
    rows.push_back({.station = {.id = *std::move(key),
                                .name = *std::move(name),
                                .location = block.location,
                                .native = std::nullopt,
                                .source = core::DataSource::user},
                    .axis = i});
  }
  std::vector<core::Variable> variables;
  variables.push_back({.meta = meta, .per_station = std::move(column)});
  auto table = core::StationTable::make(std::move(variables), std::move(axes),
                                        std::move(rows));
  if (not table) {  // ids are unique, axes increase: not reachable
    return std::unexpected{internal_error("station table")};
  }
  return Assembled{.table = *std::move(table), .warnings = std::move(warnings)};
}

std::expected<Read<ImedsFile>, ParseError> ImedsParser::finish() && {
  if (lines_ == 0) {
    return std::unexpected{
        ParseError::make(ParseErrc::empty_input, {.line = 1}, "")};
  }
  if (not header_) {
    // The first header line the file does not have.
    return std::unexpected{
        ParseError::make(ParseErrc::missing_header, {.line = lines_ + 1}, "")};
  }
  std::vector<std::string> names;
  names.reserve(stations_.size());
  for (const StationBlock& block : stations_) {
    names.push_back(block.name);
  }
  const detail::UniqueIds unique = detail::uniquify_ids(names);
  const core::SeriesMeta meta = column_meta(header_->header);
  auto assembled = assemble(std::move(stations_), meta, unique);
  if (not assembled) {
    return std::unexpected{std::move(assembled).error()};
  }
  std::vector<Warning> warnings = std::move(header_->warnings);
  warnings.insert(warnings.end(),
                  std::make_move_iterator(assembled->warnings.begin()),
                  std::make_move_iterator(assembled->warnings.end()));
  if (renamed_names_ > 0) {
    warnings.push_back({.code = WarningCode::invalid_utf8_replaced,
                        .subject = {},
                        .count = renamed_names_});
  }
  for (const detail::RenamedName& r : unique.renamed) {
    warnings.push_back({.code = WarningCode::duplicate_station_id_renamed,
                        .subject = r.name,
                        .count = r.count});
  }
  if (masked_ > 0) {
    warnings.push_back({.code = WarningCode::legacy_sentinel_masked,
                        .subject = {},
                        .count = masked_});
  }
  return Read<ImedsFile>{.value = {.header = std::move(header_->header),
                                   .table = std::move(assembled->table)},
                         .warnings = std::move(warnings)};
}

}  // namespace

std::expected<Read<ImedsFile>, ParseError> parse_imeds(std::string_view text) {
  ImedsParser parser;
  LineCursor cursor{text};
  while (const auto line = cursor.next()) {
    if (auto fed = parser.feed(*line); not fed) {
      return std::unexpected{std::move(fed).error()};
    }
  }
  return std::move(parser).finish();
}

std::expected<Read<ImedsFile>, Error> read_imeds(
    const std::filesystem::path& path, const ReadContext& ctx) {
  if (ctx.stop.stop_requested()) {
    return std::unexpected{Error{Cancelled{}}};
  }
  const auto text = read_text_file(path, ctx.limits);
  if (not text) {
    return std::unexpected{Error{text.error()}};
  }
  ImedsParser parser;
  LineCursor cursor{*text};
  while (const auto line = cursor.next()) {
    if (line->number % stop_poll_interval == 0 and ctx.stop.stop_requested()) {
      return std::unexpected{Error{Cancelled{}}};
    }
    if (auto fed = parser.feed(*line); not fed) {
      return std::unexpected{Error{std::move(fed).error()}};
    }
    if (parser.samples() > ctx.limits.max_elements) {
      return std::unexpected{Error{
          FileError{.op = FileOp::size,
                    .path = path,
                    .ec = std::make_error_code(std::errc::file_too_large)}}};
    }
  }
  return std::move(parser).finish().transform_error(lift<Error>);
}

}  // namespace mov::io
