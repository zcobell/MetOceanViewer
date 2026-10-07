// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/csv_export.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <iterator>
#include <optional>
#include <ostream>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/units.hpp"
#include "mov/io/text_file.hpp"

namespace mov::io {

namespace {

namespace chrono = std::chrono;

constexpr std::string_view header_row{
    "station_id,station_name,time_utc,quantity,value,status,unit,datum\r\n"};
constexpr std::size_t chunk_bytes = std::size_t{1} << 16;

// The characters a spreadsheet takes as the start of a formula (OWASP).
constexpr bool starts_formula(char c) {
  return c == '=' or c == '+' or c == '-' or c == '@' or c == '\t' or c == '\r';
}

// RFC 4180 quoting, with the formula guard in front of the text.
void append_text_cell(std::string& out, std::string_view text) {
  const bool guard = not text.empty() and starts_formula(text.front());
  const bool quote = text.find_first_of(",\"\r\n") != std::string_view::npos;
  if (quote) {
    out += '"';
  }
  if (guard) {
    out += '\'';
  }
  for (const char c : text) {
    if (c == '"' and quote) {
      out += '"';
    }
    out += c;
  }
  if (quote) {
    out += '"';
  }
}

std::string text_cell(std::string_view text) {
  std::string out;
  append_text_cell(out, text);
  return out;
}

void append_time(std::string& out, core::Time t) {
  const auto day = chrono::floor<chrono::days>(t);
  const chrono::year_month_day date{day};
  const chrono::hh_mm_ss<chrono::milliseconds> clock{t - day};
  const int year = static_cast<int>(date.year());
  std::format_to(
      std::back_inserter(out), "{}{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z",
      year < 0 ? "-" : "", std::abs(year), static_cast<unsigned>(date.month()),
      static_cast<unsigned>(date.day()),
      static_cast<int>(clock.hours().count()),
      static_cast<int>(clock.minutes().count()),
      static_cast<int>(clock.seconds().count()),
      static_cast<int>(clock.subseconds().count()));
}

void append_sample(std::string& out, const core::Sample& sample) {
  const std::optional<double> value = sample.value();
  if (value) {
    std::format_to(std::back_inserter(out), "{:.6f},value", *value);
  } else {
    out += sample.is_dry() ? ",dry" : ",missing";
  }
}

// The text around the time and the sample of one (station, column) pair: the
// cells that do not change from row to row.
struct RowFrame {
  std::string before_time;  // id,name,
  std::string after_time;   // ,quantity,
  std::string after_value;  // ,unit,datum CRLF
};

RowFrame frame_of(const core::FileStation& station,
                  const core::SeriesMeta& meta) {
  RowFrame frame;
  append_text_cell(frame.before_time, station.id.view());
  frame.before_time += ',';
  append_text_cell(frame.before_time, station.name.view());
  frame.before_time += ',';
  frame.after_time = ',' + text_cell(core::token(meta.quantity())) + ',';
  frame.after_value = ',';
  const std::optional<core::Unit>& unit = meta.unit();
  if (unit) {
    append_text_cell(frame.after_value, core::symbol(*unit));
  }
  frame.after_value += ',';
  const std::optional<core::VerticalDatum> datum = meta.datum();
  if (datum) {
    append_text_cell(frame.after_value, core::to_string(*datum));
  }
  frame.after_value += "\r\n";
  return frame;
}

// The CSV text, handed to `flush` in chunks of about chunk_bytes.
template <class Flush>
void emit_csv(const core::StationTable& table, Flush flush) {
  std::string buffer{header_row};
  for (const core::StationIndex i : table.stations()) {
    const std::span<const core::Time> times = table.times(i);
    for (const std::size_t k :
         std::views::iota(std::size_t{0}, table.schema().size())) {
      const RowFrame frame = frame_of(table.station(i), table.schema()[k]);
      const std::span<const core::Sample> samples =
          table.column(i, core::ColumnIndex{k});
      for (const std::size_t j :
           std::views::iota(std::size_t{0}, times.size())) {
        buffer += frame.before_time;
        append_time(buffer, times[j]);
        buffer += frame.after_time;
        append_sample(buffer, samples[j]);
        buffer += frame.after_value;
        if (buffer.size() >= chunk_bytes) {
          flush(std::string_view{buffer});
          buffer.clear();
        }
      }
    }
  }
  flush(std::string_view{buffer});
}

}  // namespace

std::string format_csv(const core::StationTable& table) {
  std::string out;
  emit_csv(table, [&out](std::string_view chunk) { out += chunk; });
  return out;
}

std::expected<void, Error> write_csv(const std::filesystem::path& path,
                                     const core::StationTable& table) {
  return write_file_atomic(
      path, [&table](std::ostream& out) -> std::expected<void, FileError> {
        emit_csv(table, [&out](std::string_view chunk) {
          out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        });
        return {};
      });
}

}  // namespace mov::io
