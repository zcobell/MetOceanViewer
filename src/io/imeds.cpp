// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/imeds.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/ascii.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/io/detail/civil_time.hpp"
#include "mov/io/detail/line_cursor.hpp"
#include "mov/io/detail/parse_at.hpp"
#include "mov/io/detail/reporting.hpp"
#include "mov/io/detail/station_names.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/text_file.hpp"

namespace mov::io {

namespace {

using detail::LineCursor;
using detail::subject_of;

// A data row has at most 7 words and a station line 3; one slot more makes
// split_ws_into report a longer line.
constexpr std::size_t word_slots = 8;
constexpr std::size_t header_lines = 3;
// Line 3 is a handful of words; a longer one is not an IMEDS header.
constexpr std::size_t max_header_line_bytes = 4096;
// How often the driver asks whether it should stop (lines, then stations).
constexpr std::size_t stop_poll_interval = 4096;
// A station costs memory as well as its rows (its block, id, name, axis and
// column vectors: a few hundred bytes, about as much as sixteen samples). The
// limit on elements counts samples plus this many per station, so a file of
// nothing but station lines cannot outgrow it.
constexpr std::size_t station_cost = 16;

// What a limit message says: how much there is and what is allowed.
ParseError too_large(std::size_t line, std::size_t used, std::size_t limit,
                     std::string_view unit) {
  return ParseError::make(ParseErrc::too_large, {.line = line},
                          std::format("{} {}, limit {}", used, unit, limit));
}

// ---- header
// ------------------------------------------------------------------

bool is_utc_token(std::string_view zone) {
  return core::ascii::equal_ignore_case(zone, "UTC") or
         core::ascii::equal_ignore_case(zone, "GMT") or
         core::ascii::equal_ignore_case(zone, "Z");
}

Read<std::string> zone_of(std::optional<std::string_view> zone) {
  const std::string_view token = zone.value_or(std::string_view{});
  Read<std::string> read{.value = std::string{token}, .warnings = {}};
  append_if_counted(read.warnings,
                    {.code = WarningCode::tz_assumed_utc,
                     .subject = subject_of(token),
                     .count = zone and is_utc_token(token) ? 0U : 1U});
  return read;
}

struct DatumAndUnit {
  std::optional<core::VerticalDatum> datum;
  std::optional<core::Unit> unit;
};

// A unit that is not in a family; the reader warns when it is not one the
// registry itself uses either.
bool is_family_unit(const core::Unit& unit) {
  return not std::holds_alternative<core::OtherUnit>(unit);
}

// The unit text is the rest of the line, so "S m-1" is one unit. "unknown" is
// what the writer says for no unit: no unit, and no warning.
Read<std::optional<core::Unit>> unit_of(std::string_view text) {
  if (core::ascii::equal_ignore_case(text, "unknown")) {
    return {.value = std::nullopt, .warnings = {}};
  }
  return detail::parsed_unit(text);
}

// The third word is the datum, with the unit after it. A v4 header may leave
// the datum out and give a unit there ("NOAA UTC ft"): a lone third word that
// is not a datum but is a unit of a family is that unit.
Read<DatumAndUnit> datum_and_unit(std::optional<std::string_view> third,
                                  std::string_view rest) {
  if (not third) {
    return {.value = {}, .warnings = {}};
  }
  const auto datum = core::parse_vertical_datum(*third);
  Read<std::optional<core::Unit>> unit = unit_of(rest);
  if (datum) {
    return {.value = {.datum = *datum, .unit = std::move(unit.value)},
            .warnings = std::move(unit.warnings)};
  }
  if (rest.empty()) {
    std::optional<core::Unit> lone = core::parse_unit(*third);
    if (lone and is_family_unit(*lone)) {
      return {.value = {.datum = std::nullopt, .unit = std::move(lone)},
              .warnings = {}};
    }
  }
  Read<DatumAndUnit> read{
      .value = {.datum = std::nullopt, .unit = std::move(unit.value)},
      .warnings = {}};
  read.warnings.push_back(
      {.code = WarningCode::datum_unknown, .subject = subject_of(*third)});
  append(read.warnings, std::move(unit.warnings));
  return read;
}

// Line 3: <source> [<time zone> [<datum> [<unit>]]]. Only the source is
// required.
std::expected<Read<ImedsHeader>, ParseError> parse_header(
    const LineCursor::Line& line) {
  if (line.text.size() > max_header_line_bytes) {
    return std::unexpected{too_large(line.number, line.text.size(),
                                     max_header_line_bytes, "bytes")};
  }
  std::string_view rest = line.text;
  const auto source = detail::next_word(rest);
  if (not source) {
    return std::unexpected{ParseError::make(ParseErrc::missing_header,
                                            {.line = line.number}, line.text)};
  }
  const auto zone = detail::next_word(rest);
  const auto third = detail::next_word(rest);
  Read<std::string> time_zone = zone_of(zone);
  Read<DatumAndUnit> rest_of_line =
      datum_and_unit(third, core::ascii::trim(rest));
  append(time_zone.warnings, std::move(rest_of_line.warnings));
  return Read<ImedsHeader>{
      .value = {.source = std::string{*source},
                .time_zone = std::move(time_zone.value),
                .datum = rest_of_line.value.datum,
                .unit = std::move(rest_of_line.value.unit)},
      .warnings = std::move(time_zone.warnings)};
}

// ---- values
// ------------------------------------------------------------------

// v4 printed its null, -DBL_MAX, as "%10.4e".
constexpr std::string_view printed_dbl_max{"-1.7977e+308"};

constexpr std::array<double, 3> legacy_sentinels{
    -99999.0, -9999.0, -std::numeric_limits<double>::max()};

bool is_legacy_sentinel(double value) {
  return std::ranges::any_of(legacy_sentinels, [value](double sentinel) {
    return std::bit_cast<std::uint64_t>(sentinel) ==
           std::bit_cast<std::uint64_t>(value);
  });
}

// "nan", "inf", "infinity" with an optional sign in any case, and a Fortran
// overflow field, a run of '*': values the model readers also read as Missing.
bool is_nonfinite_token(std::string_view token) {
  if (not token.empty() and (token.front() == '+' or token.front() == '-')) {
    token.remove_prefix(1);
  }
  const std::string lower = detail::to_lower_ascii(token);
  return lower == "nan" or lower == "inf" or lower == "infinity" or
         (not token.empty() and
          std::ranges::all_of(token, [](char c) { return c == '*'; }));
}

enum class Masked : std::uint8_t { no, legacy_sentinel, nonfinite };

struct RowValue {
  core::Sample sample;
  Masked masked;
};

std::expected<RowValue, ParseError> parse_value(const LineCursor::Line& line,
                                                std::string_view token) {
  if (core::ascii::equal_ignore_case(token, printed_dbl_max)) {
    return RowValue{.sample = core::Missing{},
                    .masked = Masked::legacy_sentinel};
  }
  if (is_nonfinite_token(token)) {
    return RowValue{.sample = core::Missing{}, .masked = Masked::nonfinite};
  }
  const auto number = detail::double_at(line, token);
  if (not number) {
    return std::unexpected{number.error()};
  }
  if (is_legacy_sentinel(*number)) {
    return RowValue{.sample = core::Missing{},
                    .masked = Masked::legacy_sentinel};
  }
  // parse_double yields only finite numbers: this is Sample::of, made total.
  return RowValue{.sample = core::finite_or_missing(*number),
                  .masked = Masked::no};
}

// ---- times
// -------------------------------------------------------------------

struct RawDate {
  int year, month, day, hour, minute, second;
};

constexpr int max_year = 9999;

// The index of the first field (in RawDate order) that is not a real date or
// time of day, or nullopt.
std::optional<std::size_t> first_bad_field(const RawDate& f) {
  if (f.year < 0 or f.year > max_year) {
    return 0;
  }
  if (f.month < 1 or f.month > 12) {
    return 1;
  }
  if (f.day < 1 or
      std::cmp_greater(f.day, detail::days_in_month(
                                  f.year, static_cast<unsigned>(f.month)))) {
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
core::Time time_of(const RawDate& f) {
  return detail::time_of({.year = f.year,
                          .month = static_cast<unsigned>(f.month),
                          .day = static_cast<unsigned>(f.day),
                          .hour = static_cast<unsigned>(f.hour),
                          .minute = static_cast<unsigned>(f.minute),
                          .second = static_cast<unsigned>(f.second),
                          .millisecond = 0});
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
  const RawDate fields{.year = numbers[0],
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
  core::StationText name;
  core::Location location;
  std::vector<core::Point> rows;
  std::size_t row_words{0};  // words of the last row (6 or 7); 0 before one
  std::size_t shape_changes{0};
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
    return std::unexpected{
        detail::position_at(line, lon_token, lat_token, location.error())};
  }
  return *location;
}

bool is_integer_token(std::string_view token) {
  const auto number = detail::parse_int<std::int64_t>(token);
  return number or number.error() == detail::NumberError::out_of_range;
}

// A 3-word line of three integers right after a row: a row cut short (a
// station line has a name and two coordinates, and a name that is a bare
// integer with integer coordinates is not worth the ambiguity).
bool looks_like_cut_row(std::span<const std::string_view> words) {
  return std::ranges::all_of(words.first(3), is_integer_token);
}

// ---- the parser
// --------------------------------------------------------------

using Words = std::array<std::string_view, word_slots>;

ParseError internal_error(std::string_view what) {
  return ParseError::make(ParseErrc::corrupt_record, {.line = 0}, what);
}

class ImedsParser {
 public:
  explicit ImedsParser(std::size_t max_elements)
      : max_elements_{max_elements} {}

  // The first three lines are the header, then station lines and rows.
  [[nodiscard]] bool in_header() const noexcept {
    return lines_ < header_lines;
  }

  // One line at a time, so that the driver can stop between lines.
  [[nodiscard]] std::expected<void, ParseError> feed(
      const LineCursor::Line& line);

  [[nodiscard]] std::expected<Read<ImedsFile>, Error> finish(
      const StopToken& stop) &&;

 private:
  [[nodiscard]] std::expected<void, ParseError> take_header(
      const LineCursor::Line& line);
  [[nodiscard]] std::expected<void, ParseError> begin_station(
      const LineCursor::Line& line, const Words& words);
  [[nodiscard]] std::expected<void, ParseError> add_row(
      const LineCursor::Line& line, const Words& words, std::size_t count);
  [[nodiscard]] std::expected<void, ParseError> charge(
      const LineCursor::Line& line, std::size_t elements);

  std::size_t max_elements_;
  std::size_t lines_{0};
  std::optional<Read<ImedsHeader>> header_;
  std::vector<StationBlock> stations_;
  bool after_row_{false};    // the last line was a data row
  std::size_t elements_{0};  // samples, plus station_cost per station
  std::size_t sentinels_masked_{0};
  std::size_t nonfinite_masked_{0};
  std::size_t names_cleaned_{0};  // names with bytes replaced
};

std::expected<void, ParseError> ImedsParser::charge(
    const LineCursor::Line& line, std::size_t elements) {
  elements_ += elements;
  if (elements_ > max_elements_) {
    return std::unexpected{
        too_large(line.number, elements_, max_elements_, "elements")};
  }
  return {};
}

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
    const LineCursor::Line& line, const Words& words) {
  const auto location = parse_location(line, words[1], words[2]);
  if (not location) {
    return std::unexpected{location.error()};
  }
  if (auto charged = charge(line, station_cost); not charged) {
    return charged;
  }
  detail::CleanedText name = detail::replace_invalid_utf8(words[0]);
  names_cleaned_ += name.replaced ? 1U : 0U;
  stations_.push_back({.name = std::move(name.text),
                       .location = *location,
                       .rows = {},
                       .row_words = 0,
                       .shape_changes = 0});
  after_row_ = false;
  return {};
}

std::expected<void, ParseError> ImedsParser::add_row(
    const LineCursor::Line& line, const Words& words, std::size_t count) {
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
  if (auto charged = charge(line, 1); not charged) {
    return charged;
  }
  sentinels_masked_ += value->masked == Masked::legacy_sentinel ? 1U : 0U;
  nonfinite_masked_ += value->masked == Masked::nonfinite ? 1U : 0U;
  StationBlock& station = stations_.back();
  station.shape_changes +=
      station.row_words != 0 and station.row_words != count ? 1U : 0U;
  station.row_words = count;
  station.rows.push_back({.time = *time, .sample = value->sample});
  after_row_ = true;
  return {};
}

std::expected<void, ParseError> ImedsParser::feed(
    const LineCursor::Line& line) {
  if (in_header()) {
    return take_header(line);
  }
  Words words{};
  const std::size_t count = detail::split_ws_into(line.text, words);
  switch (count) {
    case 3:
      if (after_row_ and looks_like_cut_row(words)) {
        return std::unexpected{
            detail::at(line, words[0], ParseErrc::wrong_field_count)};
      }
      return begin_station(line, words);
    case 6:
    case 7:
      return add_row(line, words, count);
    default:  // includes no words: the driver skips blank lines
      return std::unexpected{
          detail::at(line, words[0], ParseErrc::wrong_field_count)};
  }
}

// ---- assembling the table
// --------------------------------------------------------

// What one station block tells the user, in order.
void add_station_warnings(bool had_rows, std::size_t shape_changes,
                          const core::NormalizeReport& report,
                          const std::string& id,
                          std::vector<Warning>& warnings) {
  const std::string subject = subject_of(id);
  const auto add = [&](WarningCode code, std::size_t count) {
    append_if_counted(warnings,
                      {.code = code, .subject = subject, .count = count});
  };
  add(WarningCode::empty_station, had_rows ? 0U : 1U);
  add(WarningCode::row_shape_changed, shape_changes);
  add(WarningCode::times_reordered, report.descents);
  add(WarningCode::duplicate_times_dropped, report.duplicates_dropped);
  add(WarningCode::conflicting_duplicate_times, report.conflicting_duplicates);
}

// The table of the stations, each on its own axis. Polls `stop` every
// stop_poll_interval stations.
std::expected<Read<core::StationTable>, Error> assemble(
    std::vector<StationBlock> blocks, const core::SeriesMeta& meta,
    const detail::UniqueIds& unique, const StopToken& stop) {
  std::vector<core::TimeAxis> axes;
  std::vector<core::Column> column;
  std::vector<core::StationRow> rows;
  axes.reserve(blocks.size());
  column.reserve(blocks.size());
  rows.reserve(blocks.size());
  std::vector<Warning> warnings;
  for (const std::size_t i : std::views::iota(std::size_t{0}, blocks.size())) {
    if (i % stop_poll_interval == 0 and stop.stop_requested()) {
      return std::unexpected{Error{Cancelled{}}};
    }
    StationBlock& block = blocks[i];
    auto key = core::StationKey::make(unique.ids[i]);
    if (not key) {  // the names were cleaned and are not empty: not reachable
      return std::unexpected{Error{internal_error("station name")}};
    }
    const bool had_rows = not block.rows.empty();
    core::Normalized normalized = core::normalize(std::move(block.rows), meta);
    add_station_warnings(had_rows, block.shape_changes, normalized.report,
                         unique.ids[i], warnings);
    core::TimeSeriesParts parts = std::move(normalized.series).into_parts();
    axes.push_back(std::move(parts.times));
    column.push_back(std::move(parts.samples));
    rows.push_back({.station = {.id = *std::move(key),
                                .name = std::move(block.name),
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
    return std::unexpected{Error{internal_error("station table")}};
  }
  return Read<core::StationTable>{.value = *std::move(table),
                                  .warnings = std::move(warnings)};
}

// The column's metadata: the generic quantity with the header's unit and datum.
std::expected<core::SeriesMeta, ParseError> column_meta(
    const ImedsHeader& header) {
  const auto generic = [&header] {
    return core::SeriesMeta::make({.unit = header.unit});
  };
  if (not header.datum) {
    return generic();
  }
  // The generic quantity can carry a datum: failing here is a bug.
  auto with_datum = generic().assume_datum(*header.datum);
  if (not with_datum) {
    return std::unexpected{internal_error("datum on the generic quantity")};
  }
  return *std::move(with_datum);
}

std::expected<Read<ImedsFile>, Error> ImedsParser::finish(
    const StopToken& stop) && {
  if (lines_ == 0) {
    return std::unexpected{
        Error{ParseError::make(ParseErrc::empty_input, {.line = 1}, "")}};
  }
  if (not header_) {  // the first header line the file does not have
    return std::unexpected{Error{
        ParseError::make(ParseErrc::missing_header, {.line = lines_ + 1}, "")}};
  }
  const auto meta = column_meta(header_->value);
  if (not meta) {
    return std::unexpected{Error{meta.error()}};
  }
  std::vector<std::string> names;
  names.reserve(stations_.size());
  std::ranges::transform(
      stations_, std::back_inserter(names),
      [](const StationBlock& b) { return std::string{b.name.view()}; });
  const detail::UniqueIds unique = detail::uniquify_ids(names);
  auto table = assemble(std::move(stations_), *meta, unique, stop);
  if (not table) {
    return std::unexpected{std::move(table).error()};
  }
  std::vector<Warning> warnings = std::move(header_->warnings);
  append(warnings, std::move(table->warnings));
  append_if_counted(warnings, {.code = WarningCode::invalid_utf8_replaced,
                               .subject = {},
                               .count = names_cleaned_});
  for (const detail::RenamedName& r : unique.renamed) {
    warnings.push_back({.code = WarningCode::duplicate_station_id_renamed,
                        .subject = subject_of(r.name),
                        .count = r.count});
  }
  append_if_counted(warnings, {.code = WarningCode::legacy_sentinel_masked,
                               .subject = {},
                               .count = sentinels_masked_});
  append_if_counted(warnings, {.code = WarningCode::nonfinite_masked,
                               .subject = {},
                               .count = nonfinite_masked_});
  return Read<ImedsFile>{.value = {.header = std::move(header_->value),
                                   .table = std::move(table->value)},
                         .warnings = std::move(warnings)};
}

// The one driver loop: header lines, then the non-blank lines, with the stop
// token asked every stop_poll_interval lines. Callers ask once before they
// start.
std::expected<Read<ImedsFile>, Error> drive(std::string_view text,
                                            const ReadContext& ctx) {
  if (text.size() > ctx.limits.max_text_bytes) {
    return std::unexpected{
        Error{too_large(1, text.size(), ctx.limits.max_text_bytes, "bytes")}};
  }
  ImedsParser parser{ctx.limits.max_elements};
  LineCursor cursor{text};
  while (const auto line =
             parser.in_header() ? cursor.next() : cursor.next_nonblank()) {
    if (line->number % stop_poll_interval == 0 and ctx.stop.stop_requested()) {
      return std::unexpected{Error{Cancelled{}}};
    }
    if (auto fed = parser.feed(*line); not fed) {
      return std::unexpected{Error{std::move(fed).error()}};
    }
  }
  return std::move(parser).finish(ctx.stop);
}

}  // namespace

std::expected<Read<ImedsFile>, Error> parse_imeds(std::string_view text,
                                                  const ReadContext& ctx) {
  if (ctx.stop.stop_requested()) {
    return std::unexpected{Error{Cancelled{}}};
  }
  return drive(text, ctx);
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
  return drive(*text, ctx);
}

}  // namespace mov::io
