// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <iterator>
#include <limits>
#include <optional>
#include <ostream>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/core/detail/ascii.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/io/detail/civil_time.hpp"
#include "mov/io/imeds.hpp"
#include "mov/io/text_file.hpp"

namespace mov::io {

namespace {

namespace chrono = std::chrono;

constexpr std::size_t chunk_bytes = std::size_t{1} << 16;
// A row is 14 + 20 characters of date and value, and a line end.
constexpr std::size_t typical_row_bytes = 40;

bool separates(char c) { return core::detail::is_space(c) or c == ','; }

// Each run of white space or commas becomes one '_'.
std::string underscored(std::string_view text) {
  std::string out{text};
  const auto tail = std::ranges::unique(
      out, [](char a, char b) { return separates(a) and separates(b); });
  out.erase(tail.begin(), tail.end());
  std::ranges::replace_if(out, separates, '_');
  return out;
}

// What the writer changed or left out, for the warnings.
struct Counts {
  std::size_t ids_not_written{0};
  std::size_t omitted{0};   // Missing and Dry rows
  std::size_t floored{0};   // written rows whose milliseconds were cut
  std::size_t collided{0};  // rows dropped: same second as the one before
  std::size_t reads_as_missing{0};  // written values a reader masks
};

// The rows of station `i` that are written, in order: a value, in a second
// not yet written. `row(time, value)` gets the time floored to the second and
// returns false to stop. The counts are of what this call saw.
template <class Row>
void visit_rows(const core::StationTable& table, core::StationIndex i,
                Counts& counts, Row row) {
  const std::span<const core::Time> times = table.times(i);
  const std::span<const core::Sample> samples =
      table.column(i, core::ColumnIndex{0});
  std::optional<core::Time> previous;
  for (const std::size_t j : std::views::iota(std::size_t{0}, times.size())) {
    const std::optional<double> value = samples[j].value();
    if (not value) {
      ++counts.omitted;
      continue;
    }
    const core::Time whole = chrono::floor<chrono::seconds>(times[j]);
    if (previous == whole) {
      ++counts.collided;
      continue;
    }
    previous = whole;
    counts.floored += whole != times[j] ? 1U : 0U;
    if (not row(whole, *value)) {
      return;
    }
  }
}

void append_row(std::string& out, core::Time whole, double value) {
  const detail::CivilTime c = detail::civil_fields(whole);
  std::format_to(std::back_inserter(out),
                 "{:04} {:02} {:02} {:02} {:02} {:02} {:14.6f}\n", c.year,
                 c.month, c.day, c.hour, c.minute, c.second, value);
}

void append_station(std::string& out, const core::FileStation& station,
                    std::size_t index) {
  std::format_to(std::back_inserter(out), "{}    {:.6f}    {:.6f}\n",
                 detail::imeds_name(station.name.view(), index),
                 station.location.lat(), station.location.lon());
}

// ---- the checks that can fail, before anything is written
// ---------------------

FormatError time_error(const core::FileStation& station, std::size_t i,
                       std::size_t j) {
  return {.code = FormatErrc::time_out_of_range,
          .subject = std::string{station.id.view()},
          .station = i,
          .index = j};
}

// The first and last value rows bound all the others: times increase.
std::expected<void, FormatError> check_range(const core::StationTable& table,
                                             core::StationIndex i) {
  const std::span<const core::Time> times = table.times(i);
  const std::span<const core::Sample> samples =
      table.column(i, core::ColumnIndex{0});
  const auto is_value = [](const core::Sample& s) { return s.is_value(); };
  const auto first = std::ranges::find_if(samples, is_value);
  if (first == samples.end()) {
    return {};
  }
  const auto last =
      std::ranges::find_if(samples | std::views::reverse, is_value);
  const auto first_index = static_cast<std::size_t>(first - samples.begin());
  const auto last_index =
      static_cast<std::size_t>(last.base() - samples.begin()) - 1;
  for (const std::size_t j : {first_index, last_index}) {
    if (not detail::in_imeds_range(times[j])) {
      return std::unexpected{time_error(table.station(i), i.value(), j)};
    }
  }
  return {};
}

// A unit named "unknown" would read back as no unit.
std::expected<void, FormatError> check_unit(const core::SeriesMeta& meta) {
  const std::optional<core::Unit>& unit = meta.unit();
  if (unit and
      core::detail::equal_ignore_case(core::symbol(*unit), "unknown")) {
    return std::unexpected{FormatError{.code = FormatErrc::noncanonical_unit,
                                       .subject = "unknown"}};
  }
  return {};
}

// Validates the table and counts what the writer will do to it. After this
// succeeds emit_imeds cannot fail.
std::expected<Counts, FormatError> scan_imeds(const core::StationTable& table) {
  if (table.schema().size() != 1) {
    return std::unexpected{
        FormatError{.code = FormatErrc::wrong_column_count,
                    .subject = std::to_string(table.schema().size())}};
  }
  if (auto unit = check_unit(table.schema().front()); not unit) {
    return std::unexpected{unit.error()};
  }
  Counts counts;
  for (const core::StationIndex i : table.stations()) {
    if (auto range = check_range(table, i); not range) {
      return std::unexpected{range.error()};
    }
    const core::FileStation& station = table.station(i);
    counts.ids_not_written +=
        station.id.view() != detail::imeds_name(station.name.view(), i.value())
            ? 1U
            : 0U;
    visit_rows(table, i, counts, [&counts](core::Time, double value) {
      counts.reads_as_missing += detail::reads_as_missing(value) ? 1U : 0U;
      return true;
    });
  }
  return counts;
}

std::string header_text(const core::SeriesMeta& meta, std::string_view source) {
  const std::string cleaned = underscored(source);
  const std::optional<core::VerticalDatum> datum = meta.datum();
  const std::string_view datum_text =
      datum ? core::to_string(*datum) : std::string_view{"none"};
  const std::optional<core::Unit>& unit = meta.unit();
  const std::string_view unit_text =
      unit ? core::symbol(*unit) : std::string_view{"unknown"};
  return std::format(
      "% IMEDS generic format\n% year month day hour min sec value\n"
      "{}    UTC    {}    {}\n",
      cleaned.empty() ? std::string{"MetOceanViewer"} : cleaned, datum_text,
      unit_text);
}

// The IMEDS text of a table scan_imeds accepted, built in a buffer that is
// handed to `flush` when it holds `chunk` bytes, and at the end. flush takes
// the text out (clearing it) and returns false to stop, as a failed stream
// does.
template <class Flush>
void emit_imeds(const core::StationTable& table, std::string_view source,
                std::size_t chunk, Flush flush) {
  std::string buffer = header_text(table.schema().front(), source);
  Counts ignored;  // scan_imeds counted already
  for (const core::StationIndex i : table.stations()) {
    append_station(buffer, table.station(i), i.value());
    bool going = true;
    visit_rows(table, i, ignored, [&](core::Time whole, double value) {
      append_row(buffer, whole, value);
      going = buffer.size() < chunk or flush(buffer);
      return going;
    });
    if (not going) {
      return;
    }
  }
  flush(buffer);
}

std::vector<Warning> warnings_of(const Counts& counts) {
  std::vector<Warning> warnings;
  const auto add = [&warnings](WarningCode code, std::size_t count) {
    append_if_counted(warnings, {.code = code, .subject = {}, .count = count});
  };
  add(WarningCode::station_id_not_written, counts.ids_not_written);
  add(WarningCode::rows_omitted, counts.omitted);
  add(WarningCode::time_precision_dropped, counts.floored);
  add(WarningCode::duplicate_times_dropped, counts.collided);
  add(WarningCode::value_reads_as_missing, counts.reads_as_missing);
  return warnings;
}

}  // namespace

namespace detail {

std::string imeds_name(std::string_view name, std::size_t index) {
  const std::string cleaned = underscored(name);
  return cleaned.empty() ? std::format("station_{}", index) : cleaned;
}

bool reads_as_missing(double value) {
  if (std::bit_cast<std::uint64_t>(value) ==
      std::bit_cast<std::uint64_t>(-std::numeric_limits<double>::max())) {
    return true;
  }
  // The text of the legacy fills is short; anything longer cannot be one.
  std::array<char, 16> text{};
  const auto printed =
      std::format_to_n(text.data(), text.size(), "{:.6f}", value);
  if (std::cmp_greater(printed.size, text.size())) {
    return false;  // did not fit
  }
  const std::string_view view{text.data(),
                              static_cast<std::size_t>(printed.size)};
  return view == "-9999.000000" or view == "-99999.000000";
}

}  // namespace detail

std::expected<Read<std::string>, FormatError> format_imeds(
    const core::StationTable& table, std::string_view source) {
  const auto counts = scan_imeds(table);
  if (not counts) {
    return std::unexpected{counts.error()};
  }
  std::string out;
  out.reserve(table.total_samples() * typical_row_bytes);
  emit_imeds(table, source, std::numeric_limits<std::size_t>::max(),
             [&out](std::string& buffer) {
               out = std::move(buffer);
               return true;
             });
  return Read<std::string>{.value = std::move(out),
                           .warnings = warnings_of(*counts)};
}

std::expected<std::vector<Warning>, Error> write_imeds(
    const std::filesystem::path& path, const core::StationTable& table,
    std::string_view source) {
  const auto counts = scan_imeds(table);
  if (not counts) {
    return std::unexpected{Error{counts.error()}};
  }
  const auto written = write_file_atomic(
      path,
      [&table, source](std::ostream& out) -> std::expected<void, FileError> {
        emit_imeds(table, source, chunk_bytes, [&out](std::string& buffer) {
          out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
          buffer.clear();
          return static_cast<bool>(out);  // a full disk stops the export
        });
        return {};
      });
  if (not written) {
    return std::unexpected{written.error()};
  }
  return warnings_of(*counts);
}

}  // namespace mov::io
