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
#include <functional>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/hwm.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
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

constexpr std::size_t no_slot = static_cast<std::size_t>(-1);

// The shortest record header ("0 0\n") and station line ("1 0\n"): a text can
// hold at most this many records, whatever its header claims.
constexpr std::size_t min_line_bytes = 4;

bool is_blank(std::string_view text) noexcept {
  return detail::skip_space(text).empty();
}

// The first line with something on it, or nullopt when only blank lines
// remain. Takes the cursor by value: the caller's position is unchanged.
std::optional<Line> first_nonblank(detail::LineCursor cursor) noexcept {
  while (const auto line = cursor.next()) {
    if (not is_blank(line->text)) {
      return line;
    }
  }
  return std::nullopt;
}

// ---- header ----------------------------------------------------------------

ParseError whole_line(ParseErrc code, const Line& line) {
  return ParseError::make(code, {.line = line.number}, line.text);
}

}  // namespace

std::expected<AdcircAsciiHeader, ParseError> parse_adcirc_ascii_header(
    std::string_view text) {
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
  return AdcircAsciiHeader{
      .snapshots = *snapshots, .stations = *stations, .columns = *columns};
}

namespace {

// ---- values ----

// One station line's numbers as samples (design C9). Elevation: at or below
// the dry threshold is Dry. Every other output: it is fill, so Missing, and a
// fill in either component of a vector makes both Missing (N7). A number
// that is not finite is Missing and counted.
void classify(AdcircKind kind, std::span<const detail::ModelNumber> numbers,
              std::span<core::Sample> out, std::size_t& nonfinite) {
  bool fill = false;
  for (std::size_t j = 0; j < numbers.size(); ++j) {
    const double* raw = std::get_if<double>(&numbers[j]);
    if (raw == nullptr) {
      ++nonfinite;
      out[j] = core::Missing{};
    } else if (core::is_dry(*raw)) {
      fill = true;
      out[j] = kind == AdcircKind::elevation ? core::Sample{core::Dry{}}
                                             : core::Sample{core::Missing{}};
    } else {
      out[j] = core::finite_or_missing(*raw);
    }
  }
  if (fill and kind != AdcircKind::elevation) {
    std::ranges::fill(out.first(numbers.size()), core::Sample{core::Missing{}});
  }
}

core::SeriesMeta meta_of(core::Quantity q, core::Unit unit) {
  return core::SeriesMeta::make({.quantity = q,
                                 .label = std::string{core::info(q).long_name},
                                 .unit = std::move(unit)});
}

std::vector<core::SeriesMeta> schema_of(AdcircKind kind) {
  using core::Quantity;
  const core::Unit metre{core::LengthUnit::meter};
  const core::Unit speed{core::SpeedUnit::meter_per_second};
  switch (kind) {
    case AdcircKind::elevation:
      return {meta_of(Quantity::water_level, metre)};
    case AdcircKind::velocity:
      return {meta_of(Quantity::current_u, speed),
              meta_of(Quantity::current_v, speed)};
    case AdcircKind::pressure:
      return {meta_of(Quantity::air_pressure,
                      core::Unit{core::PressureUnit::meter_of_water})};
    case AdcircKind::wind:
      return {meta_of(Quantity::wind_u, speed),
              meta_of(Quantity::wind_v, speed)};
  }
  return {};
}

// ---- time axis ----

struct AxisReport {
  std::size_t descents{};
  std::size_t duplicates_dropped{};
  std::size_t conflicting{};
};

// Whether snapshots a and b differ in any column.
bool snapshots_differ(const std::vector<core::Column>& columns, std::size_t a,
                      std::size_t b) {
  return std::ranges::any_of(
      columns, [a, b](const core::Column& c) { return c[a] != c[b]; });
}

// Makes the record times strictly increasing, as a restart that overlaps its
// predecessor needs: sorts the records by time (stably), keeps the first of
// each run of equal times, and moves the columns along. Records already in
// order are left alone.
AxisReport make_increasing(std::vector<core::Time>& times,
                           std::vector<core::Column>& columns) {
  const auto in_order = [](core::Time a, core::Time b) { return a < b; };
  if (std::ranges::adjacent_find(times, std::not_fn(in_order)) == times.end()) {
    return {};
  }
  AxisReport report;
  for (std::size_t i = 1; i < times.size(); ++i) {
    report.descents += times[i] < times[i - 1] ? 1U : 0U;
  }
  std::vector<std::size_t> order(times.size());
  std::ranges::copy(std::views::iota(std::size_t{0}, times.size()),
                    order.begin());
  std::ranges::stable_sort(order, {},
                           [&times](std::size_t i) { return times[i]; });
  std::vector<std::size_t> kept;
  kept.reserve(order.size());
  for (const std::size_t i : order) {
    if (not kept.empty() and times[kept.back()] == times[i]) {
      ++report.duplicates_dropped;
      report.conflicting += snapshots_differ(columns, kept.back(), i) ? 1U : 0U;
    } else {
      kept.push_back(i);
    }
  }
  const auto gather = [&kept](const auto& from) {
    std::remove_cvref_t<decltype(from)> to;
    to.reserve(kept.size());
    std::ranges::transform(kept, std::back_inserter(to),
                           [&from](std::size_t i) { return from[i]; });
    return to;
  };
  times = gather(times);
  for (core::Column& column : columns) {
    column = gather(column);
  }
  return report;
}

// ---- the records ----

enum class Outcome : std::uint8_t {
  complete,   // a whole record is in the scratch row
  clean_end,  // the text ended between records
  cut_off,    // the text ended inside a record
  malformed,  // a line is not what the record needs (see malformed_)
};

class AsciiReader {
 public:
  AsciiReader(std::string_view text,
              std::span<const core::FileStation> stations,
              const AdcircAsciiRequest& request, const ReadContext& ctx,
              const AdcircAsciiHeader& header)
      : text_{text},
        cursor_{text},
        stations_{stations},
        request_{request},
        ctx_{ctx},
        header_{header},
        value_columns_{column_count(request.kind)},
        selected_{request.stations.indices().size()},
        ends_with_newline_{text.ends_with('\n')} {}

  std::expected<Read<core::StationTable>, Error> read() {
    skip_description_and_header();
    prepare();
    if (auto stopped = read_records(); not stopped) {
      return std::unexpected{std::move(stopped.error())};
    }
    return assemble();
  }

 private:
  void skip_description_and_header() {
    // parse_adcirc_ascii_header has read both lines already.
    static_cast<void>(cursor_.next());
    static_cast<void>(cursor_.next());
  }

  void prepare() {
    slot_.assign(header_.stations, no_slot);
    const auto picked = request_.stations.indices();
    for (std::size_t p = 0; p < picked.size(); ++p) {
      slot_[picked[p]] = p;
    }
    columns_.resize(value_columns_ * selected_);
    row_.resize(value_columns_ * selected_);
    // A header can claim any number of records; the text can hold only so many.
    const std::size_t room =
        text_.size() / (min_line_bytes * (1 + header_.stations));
    const std::size_t expected = std::min(header_.snapshots, room);
    times_.reserve(expected);
    for (core::Column& column : columns_) {
      column.reserve(expected);
    }
  }

  std::expected<void, Error> read_records() {
    for (std::size_t record = 0; record < header_.snapshots; ++record) {
      if (ctx_.stop.stop_requested()) {
        return std::unexpected{Error{Cancelled{}}};
      }
      const auto outcome = read_record(record);
      if (not outcome) {
        return std::unexpected{outcome.error()};
      }
      switch (*outcome) {
        case Outcome::complete:
          if (auto fits = commit(); not fits) {
            return fits;
          }
          continue;
        case Outcome::clean_end:
          return {};
        case Outcome::cut_off:
          partial_ = true;
          return {};
        case Outcome::malformed:
          // The last thing in the text is a cut-off record; anything after a
          // malformed line means the file is damaged.
          if (malformed_ and first_nonblank(cursor_)) {
            return std::unexpected{Error{*malformed_}};
          }
          partial_ = true;
          return {};
      }
    }
    if (const auto extra = first_nonblank(cursor_)) {
      return std::unexpected{
          Error{whole_line(ParseErrc::trailing_text, *extra)}};
    }
    return {};
  }

  // True when the text ends in the middle of this line: it has no newline and
  // does not finish the run (a run that was cut off by a copy or a crash ends
  // mid-line far more often than a finished one lacks its last newline).
  [[nodiscard]] bool cut_in_line(bool finishes_run) const noexcept {
    return cursor_.at_end() and not ends_with_newline_ and not finishes_run;
  }

  Outcome malformed_line(const Line& line) {
    malformed_ = whole_line(ParseErrc::corrupt_record, line);
    return Outcome::malformed;
  }

  Outcome malformed_at(const Line& line, std::string_view token) {
    malformed_ = detail::at(line, token, ParseErrc::corrupt_record);
    return Outcome::malformed;
  }

  std::expected<Outcome, Error> read_record(std::size_t record) {
    const auto head = cursor_.next();
    if (not head) {
      return Outcome::clean_end;
    }
    if (is_blank(head->text)) {
      return first_nonblank(cursor_) ? malformed_line(*head)
                                     : Outcome::clean_end;
    }
    const bool last_record = record + 1 == header_.snapshots;
    if (cut_in_line(last_record and header_.stations == 0)) {
      return Outcome::cut_off;
    }
    std::array<std::string_view, 2> words{};
    if (detail::split_ws_into(head->text, words) != words.size()) {
      return malformed_line(*head);
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
          Error{detail::at(*head, words[0], ParseErrc::time_out_of_range)}};
    }
    pending_time_ = *time;
    return read_stations(last_record);
  }

  Outcome read_stations(bool last_record) {
    for (std::size_t i = 0; i < header_.stations; ++i) {
      const auto line = cursor_.next();
      if (not line) {
        return Outcome::cut_off;
      }
      if (cut_in_line(last_record and i + 1 == header_.stations)) {
        return Outcome::cut_off;
      }
      // The lines of stations nobody selected are skipped, not read.
      if (slot_[i] == no_slot) {
        continue;
      }
      if (const auto bad = read_values(*line, slot_[i])) {
        return *bad;
      }
    }
    return Outcome::complete;
  }

  // Fills this station's cells of the scratch row; nullopt, or what is wrong.
  std::optional<Outcome> read_values(const Line& line, std::size_t slot) {
    std::array<std::string_view, 3> words{};
    const std::span<std::string_view> room =
        std::span{words}.first(value_columns_ + 1);
    if (detail::split_ws_into(line.text, room) != room.size()) {
      return malformed_line(line);
    }
    std::array<detail::ModelNumber, 2> numbers;
    for (std::size_t j = 0; j < value_columns_; ++j) {
      const auto number = detail::parse_model_number(words[1 + j]);
      if (not number) {
        return malformed_at(line, words[1 + j]);
      }
      numbers[j] = *number;
    }
    std::array<core::Sample, 2> samples;
    classify(request_.kind, std::span{numbers}.first(value_columns_), samples,
             nonfinite_);
    for (std::size_t j = 0; j < value_columns_; ++j) {
      row_[j * selected_ + slot] = samples[j];
    }
    return std::nullopt;
  }

  std::expected<void, Error> commit() {
    times_.push_back(pending_time_);
    for (std::size_t k = 0; k < columns_.size(); ++k) {
      columns_[k].push_back(row_[k]);
    }
    if (not row_.empty() and
        times_.size() > ctx_.limits.max_elements / row_.size()) {
      return std::unexpected{Error{ParseError::make(
          ParseErrc::out_of_range, {.line = cursor_.lines_read()},
          "more samples than ReadLimits::max_elements")}};
    }
    return {};
  }

  [[nodiscard]] std::vector<Warning> warnings(
      const AxisReport& axis, std::size_t complete_records) const {
    std::vector<Warning> out;
    const auto add = [&out](WarningCode code, std::string subject,
                            std::size_t count) {
      if (count > 0) {
        out.push_back(Warning{
            .code = code, .subject = std::move(subject), .count = count});
      }
    };
    add(WarningCode::nonfinite_masked, {}, nonfinite_);
    if (partial_) {
      add(WarningCode::partial_record_dropped,
          "record " + std::to_string(complete_records + 1), 1);
    }
    if (complete_records < header_.snapshots) {
      add(WarningCode::fewer_snapshots_than_header,
          "NSnaps " + std::to_string(header_.snapshots) + ", read " +
              std::to_string(complete_records),
          header_.snapshots - complete_records);
    }
    add(WarningCode::times_reordered, {}, axis.descents);
    add(WarningCode::duplicate_times_dropped, {}, axis.duplicates_dropped);
    add(WarningCode::conflicting_duplicate_times, {}, axis.conflicting);
    return out;
  }

  std::expected<Read<core::StationTable>, Error> assemble() {
    const std::size_t complete_records = times_.size();
    const AxisReport axis = make_increasing(times_, columns_);
    std::vector<Warning> notes = warnings(axis, complete_records);

    std::vector<core::Variable> variables;
    std::vector<core::SeriesMeta> schema = schema_of(request_.kind);
    for (std::size_t j = 0; j < value_columns_; ++j) {
      core::Variable variable{.meta = std::move(schema[j]), .per_station = {}};
      variable.per_station.reserve(selected_);
      for (std::size_t p = 0; p < selected_; ++p) {
        variable.per_station.push_back(std::move(columns_[j * selected_ + p]));
      }
      variables.push_back(std::move(variable));
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
      return std::unexpected{Error{detail::to_format_error(table.error())}};
    }
    return Read<core::StationTable>{.value = *std::move(table),
                                    .warnings = std::move(notes)};
  }

  std::string_view text_;
  detail::LineCursor cursor_;
  std::span<const core::FileStation> stations_;
  const AdcircAsciiRequest& request_;
  const ReadContext& ctx_;
  AdcircAsciiHeader header_;
  std::size_t value_columns_;
  std::size_t selected_;
  bool ends_with_newline_;

  std::vector<std::size_t> slot_;      // station index -> selected position
  std::vector<core::Time> times_;      // one per complete record
  std::vector<core::Column> columns_;  // [value column * selected + position]
  std::vector<core::Sample> row_;      // the record being read, same layout
  core::Time pending_time_{};
  std::optional<ParseError> malformed_;
  bool partial_{false};
  std::size_t nonfinite_{0};
};

FormatError format_error(FormatErrc code, std::string subject) {
  return FormatError{.code = code,
                     .subject = std::move(subject),
                     .station = std::nullopt,
                     .index = std::nullopt};
}

}  // namespace

std::expected<Read<core::StationTable>, Error> parse_adcirc_ascii(
    std::string_view text, std::span<const core::FileStation> stations,
    const AdcircAsciiRequest& request, const ReadContext& ctx) {
  const auto header = parse_adcirc_ascii_header(text);
  if (not header) {
    return std::unexpected{Error{header.error()}};
  }
  if (header->columns != column_count(request.kind)) {
    return std::unexpected{
        Error{format_error(FormatErrc::wrong_column_count, "NCOLS")}};
  }
  if (stations.size() != header->stations) {
    return std::unexpected{
        Error{format_error(FormatErrc::station_count_mismatch, "NStations")}};
  }
  if (not request.stations.applies_to(header->stations)) {
    return std::unexpected{
        Error{format_error(FormatErrc::station_count_mismatch, "selection")}};
  }
  return AsciiReader{text, stations, request, ctx, *header}.read();
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
                           return std::unexpected{Error{text.error()}};
                         }
                         return parse_adcirc_ascii(*text, stations, request,
                                                   ctx);
                       });
}

}  // namespace mov::io
