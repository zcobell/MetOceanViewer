// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/adcirc_ascii.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/hwm.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/io/detail/adcirc_schema.hpp"
#include "mov/io/detail/line_cursor.hpp"
#include "mov/io/detail/model_number.hpp"
#include "mov/io/detail/parse_at.hpp"
#include "mov/io/detail/table_error.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/text_file.hpp"
#include "mov/io/warning.hpp"

namespace mov::io {

namespace {

using Line = detail::LineCursor::Line;

template <class E>
auto fail(E&& e) {
  return std::unexpected{lift<Error>(std::forward<E>(e))};
}

bool blank_text(std::string_view text) noexcept {
  return detail::skip_space(text).empty();
}

// The first line with something on it, or nullopt when only blank lines
// remain. Takes the cursor by value: the caller's position is unchanged.
std::optional<Line> first_nonblank(detail::LineCursor cursor) noexcept {
  while (const auto line = cursor.next()) {
    if (not blank_text(line->text)) {
      return line;
    }
  }
  return std::nullopt;
}

ParseError whole_line(ParseErrc code, const Line& line) {
  return ParseError::make(code, {.line = line.number}, line.text);
}

// ---- header ----------------------------------------------------------------

// The header, and the text after it.
struct HeaderAndRest {
  AdcircAsciiHeader header;
  detail::LineCursor rest;
};

std::expected<HeaderAndRest, ParseError> read_header(std::string_view text) {
  detail::LineCursor cursor{text};
  if (not cursor.next()) {  // the run description, which is not read
    return std::unexpected{
        ParseError::make(ParseErrc::empty_input, {.line = 1}, "")};
  }
  const auto line = cursor.next();
  if (not line) {
    return std::unexpected{
        ParseError::make(ParseErrc::missing_header, {.line = 2}, "")};
  }
  std::array<std::string_view, 5> fields{};
  if (detail::split_ws_into(line->text, fields) < fields.size()) {
    return std::unexpected{whole_line(ParseErrc::wrong_field_count, *line)};
  }
  const auto snapshots = detail::int_at<std::size_t>(*line, fields[0]);
  const auto stations = detail::int_at<std::size_t>(*line, fields[1]);
  const auto columns = detail::int_at<std::size_t>(*line, fields[4]);
  if (not snapshots) {
    return std::unexpected{snapshots.error()};
  }
  if (not stations) {
    return std::unexpected{stations.error()};
  }
  if (not columns) {
    return std::unexpected{columns.error()};
  }
  return HeaderAndRest{.header = {.snapshots = *snapshots,
                                  .stations = *stations,
                                  .columns = *columns},
                       .rest = cursor};
}

// ---- values
// ------------------------------------------------------------------

// The model's "no data" test on a finite number: the same threshold as the
// dry rule, which only elevation turns into Dry (design C9).
bool is_fill(double raw) noexcept { return core::is_dry(raw); }

struct Classified {
  std::array<core::Sample, 2> samples;
  std::size_t nonfinite;  // tokens that were NaN, Inf or ****
};

// One station line's numbers as samples. Elevation: a fill is Dry. Every
// other output: it is Missing, and a fill in either component of a vector
// makes both Missing (N7). A number that is not finite is Missing and counted.
Classified classify(AdcircKind kind,
                    std::span<const detail::ModelNumber> numbers) {
  Classified out{.samples = {}, .nonfinite = 0};
  bool fill = false;
  for (std::size_t j = 0; j < numbers.size(); ++j) {
    const double* raw = std::get_if<double>(&numbers[j]);
    if (raw == nullptr) {
      ++out.nonfinite;
    } else if (is_fill(*raw)) {
      fill = true;
      out.samples[j] = kind == AdcircKind::elevation
                           ? core::Sample{core::Dry{}}
                           : core::Sample{core::Missing{}};
    } else {
      out.samples[j] = core::finite_or_missing(*raw);
    }
  }
  if (fill and kind != AdcircKind::elevation) {
    out.samples.fill(core::Sample{core::Missing{}});
  }
  return out;
}

// ---- the records
// -----------------------------------------------------------------

// What reading one record found. A Malformed record is only an error if more
// text follows it.
struct Complete {
  core::Time time;
};
struct CleanEnd {};  // the text ended between records
struct CutOff {};    // the text ended inside a record
struct Malformed {
  ParseError error;
};
using RecordOutcome = std::variant<Complete, CleanEnd, CutOff, Malformed>;

enum class Ending : std::uint8_t { clean, cut_off };

// The scratch row and the columns are [variable][selected position], like
// core::Variable::per_station.
using Row = std::vector<std::vector<core::Sample>>;
using Columns = std::vector<std::vector<core::Column>>;

// Station index -> position in the selection, for the stations selected.
std::vector<std::optional<std::size_t>> slots_of(
    const core::StationSelection& picked, std::size_t stations) {
  std::vector<std::optional<std::size_t>> slots(stations);
  const auto indices = picked.indices();
  for (std::size_t p = 0; p < indices.size(); ++p) {
    slots[indices[p]] = p;
  }
  return slots;
}

class AsciiReader {
 public:
  AsciiReader(std::string_view text, HeaderAndRest parsed,
              std::span<const core::FileStation> stations,
              const AdcircAsciiRequest& request, const ReadContext& ctx)
      : cursor_{parsed.rest},
        header_{parsed.header},
        stations_{stations},
        request_{request},
        ctx_{ctx},
        value_columns_{column_count(request.kind)},
        selected_{request.stations.indices().size()},
        ends_with_newline_{text.ends_with('\n')},
        slot_{slots_of(request.stations, header_.stations)},
        row_(value_columns_, std::vector<core::Sample>(selected_)),
        columns_(value_columns_, std::vector<core::Column>(selected_)) {}

  std::expected<Read<core::StationTable>, Error> read() && {
    const auto ended = read_records();
    if (not ended) {
      return std::unexpected{ended.error()};
    }
    return std::move(*this).assemble(*ended);
  }

 private:
  std::expected<Ending, Error> read_records() {
    for (;;) {
      if (ctx_.stop.stop_requested()) {
        return std::unexpected{Error{Cancelled{}}};
      }
      const auto outcome = read_record();
      if (not outcome) {
        return fail(outcome.error());
      }
      if (const auto* done = std::get_if<Complete>(&*outcome)) {
        if (auto fits = commit(done->time); not fits) {
          return std::unexpected{std::move(fits.error())};
        }
      } else if (std::holds_alternative<CleanEnd>(*outcome)) {
        return Ending::clean;
      } else if (std::holds_alternative<CutOff>(*outcome)) {
        return Ending::cut_off;
      } else {
        // The last thing in the text is a cut-off record; anything after a
        // malformed line means the file is damaged.
        if (first_nonblank(cursor_)) {
          return fail(std::get<Malformed>(*outcome).error);
        }
        return Ending::cut_off;
      }
    }
  }

  // True when the line just read is the last of a text with no final
  // newline. A finished ADCIRC file always ends with one, and a file cut by a
  // crash or a copy ends mid-line, where a truncated number still parses.
  [[nodiscard]] bool cut_in_line() const noexcept {
    return cursor_.at_end() and not ends_with_newline_;
  }

  // ParseError: a record time that is a number but not a time.
  std::expected<RecordOutcome, ParseError> read_record() {
    record_start_ = cursor_.remaining_bytes();
    pending_nonfinite_ = 0;
    pending_first_masked_ = 0;
    const auto head = cursor_.next();
    if (not head) {
      return RecordOutcome{CleanEnd{}};
    }
    if (is_blank(*head)) {
      return first_nonblank(cursor_) ? RecordOutcome{Malformed{whole_line(
                                           ParseErrc::corrupt_record, *head)}}
                                     : RecordOutcome{CleanEnd{}};
    }
    if (cut_in_line()) {
      return RecordOutcome{CutOff{}};
    }
    std::array<std::string_view, 2> words{};
    if (detail::split_ws_into(head->text, words) != words.size()) {
      return RecordOutcome{
          Malformed{whole_line(ParseErrc::corrupt_record, *head)}};
    }
    const auto seconds = detail::parse_model_number(words[0]);
    const double* const record_seconds =
        seconds ? std::get_if<double>(&*seconds) : nullptr;
    if (record_seconds == nullptr) {
      return malformed_at(*head, words[0]);
    }
    if (not detail::parse_int<std::int64_t>(words[1])) {
      return malformed_at(*head, words[1]);
    }
    const auto time = core::checked_time(
        *record_seconds, std::chrono::milliseconds{1000}, request_.cold_start);
    if (not time) {
      return std::unexpected{
          detail::at(*head, words[0], ParseErrc::time_out_of_range)};
    }
    return read_stations(*time);
  }

  static bool is_blank(const Line& line) noexcept {
    return blank_text(line.text);
  }

  static RecordOutcome malformed_at(const Line& line, std::string_view token) {
    return Malformed{detail::at(line, token, ParseErrc::corrupt_record)};
  }

  RecordOutcome read_stations(core::Time time) {
    for (std::size_t i = 0; i < header_.stations; ++i) {
      const auto line = cursor_.next();
      if (not line or cut_in_line()) {
        return CutOff{};
      }
      // A blank line is never a station, selected or not; the other lines of
      // stations nobody selected are skipped, not read.
      if (is_blank(*line)) {
        return Malformed{whole_line(ParseErrc::corrupt_record, *line)};
      }
      const std::optional<std::size_t>& slot = slot_[i];
      if (not slot) {
        continue;
      }
      if (const auto read = read_values(*line, i, *slot); not read) {
        return Malformed{read.error()};
      }
    }
    return Complete{time};
  }

  // Fills this station's cells of the scratch row.
  std::expected<void, ParseError> read_values(const Line& line,
                                              std::size_t station,
                                              std::size_t position) {
    std::array<std::string_view, 3> words{};
    const std::span<std::string_view> room =
        std::span{words}.first(value_columns_ + 1);
    if (detail::split_ws_into(line.text, room) != room.size()) {
      return std::unexpected{whole_line(ParseErrc::corrupt_record, line)};
    }
    // Stations are numbered 1..N in order: a line out of step is a record
    // whose lines are missing or doubled.
    const auto index = detail::parse_int<std::size_t>(words[0]);
    if (not index or *index != station + 1) {
      return std::unexpected{
          detail::at(line, words[0], ParseErrc::corrupt_record)};
    }
    std::array<detail::ModelNumber, 2> numbers;
    for (std::size_t j = 0; j < value_columns_; ++j) {
      const auto number = detail::parse_model_number(words[1 + j]);
      if (not number) {
        return std::unexpected{
            detail::at(line, words[1 + j], ParseErrc::corrupt_record)};
      }
      numbers[j] = *number;
    }
    const Classified classified =
        classify(request_.kind, std::span{numbers}.first(value_columns_));
    if (classified.nonfinite > 0 and pending_nonfinite_ == 0) {
      pending_first_masked_ = line.number;
    }
    pending_nonfinite_ += classified.nonfinite;
    for (std::size_t j = 0; j < value_columns_; ++j) {
      row_[j][position] = classified.samples[j];
    }
    return {};
  }

  std::expected<void, Error> commit(core::Time time) {
    times_.push_back(time);
    for (std::size_t j = 0; j < value_columns_; ++j) {
      for (std::size_t p = 0; p < selected_; ++p) {
        columns_[j][p].push_back(row_[j][p]);
      }
    }
    if (pending_nonfinite_ > 0 and nonfinite_ == 0) {
      first_masked_line_ = pending_first_masked_;
    }
    nonfinite_ += pending_nonfinite_;
    const std::size_t cells = value_columns_ * selected_;
    if (cells > 0 and times_.size() > ctx_.limits.max_elements / cells) {
      return fail(
          ParseError::make(ParseErrc::too_large, {.line = cursor_.lines_read()},
                           "more samples than ReadLimits::max_elements"));
    }
    if (times_.size() == 1) {
      reserve_for_the_rest(cells);
    }
    return {};
  }

  // The first record is the only evidence of how big a record is: the rest of
  // the text can hold about remaining / size more. The header's NSnaps is a
  // hint, never a promise, and the limit still applies.
  void reserve_for_the_rest(std::size_t cells) {
    const std::size_t first_bytes =
        std::max<std::size_t>(record_start_ - cursor_.remaining_bytes(), 1);
    const std::size_t by_text = cursor_.remaining_bytes() / first_bytes + 1;
    const std::size_t by_limit =
        cells > 0 ? ctx_.limits.max_elements / cells : by_text;
    const std::size_t cap = std::min(by_text, by_limit);
    detail::reserve_capped(times_, header_.snapshots, cap);
    for (auto& variable : columns_) {
      for (core::Column& column : variable) {
        detail::reserve_capped(column, header_.snapshots, cap);
      }
    }
  }

  [[nodiscard]] bool rows_differ(std::size_t a, std::size_t b) const {
    return std::ranges::any_of(columns_, [a, b](const auto& variable) {
      return std::ranges::any_of(
          variable, [a, b](const core::Column& c) { return c[a] != c[b]; });
    });
  }

  // Keeps the records `kept` lists, in that order.
  void keep_records(std::span<const std::size_t> kept) {
    const auto gather = [kept](auto& values) {
      std::remove_cvref_t<decltype(values)> out;
      out.reserve(kept.size());
      for (const std::size_t i : kept) {
        out.push_back(values[i]);
      }
      values = std::move(out);
    };
    gather(times_);
    for (auto& variable : columns_) {
      for (core::Column& column : variable) {
        gather(column);
      }
    }
  }

  [[nodiscard]] std::vector<Warning> warnings(
      Ending ended, std::size_t complete_records,
      const core::NormalizeReport& axis) const {
    std::vector<Warning> out;
    const auto add = [&out](WarningCode code, std::string subject,
                            std::size_t count) {
      if (count > 0) {
        out.push_back(Warning{
            .code = code, .subject = std::move(subject), .count = count});
      }
    };
    add(WarningCode::nonfinite_masked,
        "first at line " + std::to_string(first_masked_line_), nonfinite_);
    if (ended == Ending::cut_off) {
      add(WarningCode::partial_record_dropped,
          "record " + std::to_string(complete_records + 1), 1);
    }
    const std::string counts = "NSnaps " + std::to_string(header_.snapshots) +
                               ", read " + std::to_string(complete_records);
    if (complete_records < header_.snapshots) {
      add(WarningCode::fewer_snapshots_than_header, counts,
          header_.snapshots - complete_records);
    }
    if (complete_records > header_.snapshots) {
      add(WarningCode::more_snapshots_than_header, counts,
          complete_records - header_.snapshots);
    }
    add(WarningCode::times_reordered, {}, axis.descents);
    add(WarningCode::duplicate_times_dropped, {}, axis.duplicates_dropped);
    add(WarningCode::conflicting_duplicate_times, {},
        axis.conflicting_duplicates);
    return out;
  }

  std::expected<Read<core::StationTable>, Error> assemble(Ending ended) && {
    const std::size_t complete_records = times_.size();
    const core::NormalizingOrder order = core::normalizing_order(
        times_, [this](std::size_t kept, std::size_t dropped) {
          return rows_differ(kept, dropped);
        });
    if (not order.report.clean()) {
      keep_records(order.kept);
    }
    std::vector<Warning> notes =
        warnings(ended, complete_records, order.report);

    std::vector<core::Variable> variables;
    std::vector<core::SeriesMeta> schema = detail::adcirc_schema(request_.kind);
    variables.reserve(value_columns_);
    for (std::size_t j = 0; j < value_columns_; ++j) {
      variables.push_back({.meta = std::move(schema[j]),
                           .per_station = std::move(columns_[j])});
    }
    std::vector<core::StationRow> rows;
    rows.reserve(selected_);
    for (const std::size_t index : request_.stations.indices()) {
      rows.push_back({.station = stations_[index], .axis = 0});
    }
    std::vector<core::TimeAxis> axes;
    axes.push_back(std::move(times_));
    auto table = core::StationTable::make(std::move(variables), std::move(axes),
                                          std::move(rows));
    if (not table) {
      return fail(detail::to_format_error(table.error()));
    }
    return Read<core::StationTable>{.value = *std::move(table),
                                    .warnings = std::move(notes)};
  }

  detail::LineCursor cursor_;
  AdcircAsciiHeader header_;
  std::span<const core::FileStation> stations_;
  const AdcircAsciiRequest& request_;
  const ReadContext& ctx_;
  std::size_t value_columns_;
  std::size_t selected_;
  bool ends_with_newline_;
  std::vector<std::optional<std::size_t>> slot_;
  Row row_;                        // the record being read
  Columns columns_;                // the complete records
  std::vector<core::Time> times_;  // one per complete record

  std::size_t record_start_{0};  // bytes left when the record began
  std::size_t pending_nonfinite_{0};
  std::size_t pending_first_masked_{0};
  std::size_t nonfinite_{0};
  std::size_t first_masked_line_{0};
};

FormatError format_error(FormatErrc code, std::string subject) {
  return FormatError{.code = code,
                     .subject = std::move(subject),
                     .station = std::nullopt,
                     .index = std::nullopt};
}

}  // namespace

std::expected<AdcircAsciiHeader, ParseError> parse_adcirc_ascii_header(
    std::string_view text) {
  return read_header(text).transform(
      [](const HeaderAndRest& parsed) { return parsed.header; });
}

std::expected<Read<core::StationTable>, Error> parse_adcirc_ascii(
    std::string_view text, std::span<const core::FileStation> stations,
    const AdcircAsciiRequest& request, const ReadContext& ctx) {
  auto parsed = read_header(text);
  if (not parsed) {
    return fail(parsed.error());
  }
  const AdcircAsciiHeader& header = parsed->header;
  if (header.columns != column_count(request.kind)) {
    return fail(format_error(FormatErrc::wrong_column_count,
                             "NCOLS is " + std::to_string(header.columns) +
                                 ", this output has " +
                                 std::to_string(column_count(request.kind))));
  }
  if (stations.size() != header.stations) {
    return fail(format_error(FormatErrc::station_count_mismatch,
                             "NStations is " + std::to_string(header.stations) +
                                 ", the station list has " +
                                 std::to_string(stations.size())));
  }
  if (not request.stations.applies_to(header.stations)) {
    return fail(
        format_error(FormatErrc::station_count_mismatch,
                     "NStations is " + std::to_string(header.stations) +
                         ", the selection is for " +
                         std::to_string(request.stations.station_count())));
  }
  return AsciiReader{text, *std::move(parsed), stations, request, ctx}.read();
}

std::expected<Read<core::StationTable>, Error> read_adcirc_ascii(
    const std::filesystem::path& output,
    const std::filesystem::path& station_file, core::Epsg crs,
    const AdcircAsciiRequest& request, const ReadContext& ctx) {
  return and_then_read(read_adcirc_station_file(station_file, crs, ctx),
                       [&](const std::vector<core::FileStation>& stations)
                           -> std::expected<Read<core::StationTable>, Error> {
                         const auto text = read_text_file(output, ctx.limits);
                         if (not text) {
                           return fail(text.error());
                         }
                         return parse_adcirc_ascii(*text, stations, request,
                                                   ctx);
                       });
}

}  // namespace mov::io
