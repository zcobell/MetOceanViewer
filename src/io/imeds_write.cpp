// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <iterator>
#include <optional>
#include <ostream>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/core/detail/ascii.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/io/imeds.hpp"
#include "mov/io/text_file.hpp"

namespace mov::io {

namespace {

namespace chrono = std::chrono;

constexpr int max_year = 9999;

bool separates(char c) { return core::detail::is_space(c) or c == ','; }

// Each run of white space or commas becomes one '_'.
std::string underscored(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  bool in_run = false;
  for (const char c : text) {
    if (separates(c)) {
      if (not in_run) {
        out += '_';
      }
      in_run = true;
    } else {
      out += c;
      in_run = false;
    }
  }
  return out;
}

struct Counts {
  std::size_t floored{0};   // rows whose milliseconds were cut
  std::size_t collided{0};  // rows dropped: same second as the one before
};

// One printed time: whole seconds, as the pieces of the row.
struct Stamp {
  chrono::sys_seconds seconds{};
  bool floored{false};
};

Stamp stamp_of(core::Time t) {
  const auto whole = chrono::floor<chrono::seconds>(t);
  return {.seconds = whole, .floored = whole != t};
}

bool year_fits(chrono::sys_seconds t) {
  const int year = static_cast<int>(
      chrono::year_month_day{chrono::floor<chrono::days>(t)}.year());
  return year >= 0 and year <= max_year;
}

void append_row(std::string& out, chrono::sys_seconds t, double value) {
  const auto day = chrono::floor<chrono::days>(t);
  const chrono::year_month_day date{day};
  const chrono::hh_mm_ss<chrono::seconds> clock{t - day};
  std::format_to(
      std::back_inserter(out), "{:04} {:02} {:02} {:02} {:02} {:02} {:14.6f}\n",
      static_cast<int>(date.year()), static_cast<unsigned>(date.month()),
      static_cast<unsigned>(date.day()),
      static_cast<int>(clock.hours().count()),
      static_cast<int>(clock.minutes().count()),
      static_cast<int>(clock.seconds().count()), value);
}

FormatError time_error(const core::FileStation& station, std::size_t i,
                       std::size_t j) {
  return {.code = FormatErrc::time_out_of_range,
          .subject = std::string{station.id.view()},
          .station = i,
          .index = j};
}

// The rows of station i, appended to `out`.
std::expected<void, FormatError> append_station(std::string& out,
                                                const core::StationTable& table,
                                                core::StationIndex i,
                                                Counts& counts) {
  const core::FileStation& station = table.station(i);
  out += std::format("{}    {:.6f}    {:.6f}\n",
                     detail::imeds_name(station.name.view(), i.value()),
                     station.location.lat(), station.location.lon());
  const std::span<const core::Time> times = table.times(i);
  const std::span<const core::Sample> samples =
      table.column(i, core::ColumnIndex{0});
  std::optional<chrono::sys_seconds> previous;
  for (const std::size_t j : std::views::iota(std::size_t{0}, times.size())) {
    const std::optional<double> value = samples[j].value();
    if (not value) {
      continue;  // Missing and Dry have no IMEDS spelling (N18)
    }
    const Stamp stamp = stamp_of(times[j]);
    if (not year_fits(stamp.seconds)) {
      return std::unexpected{time_error(station, i.value(), j)};
    }
    counts.floored += stamp.floored ? 1U : 0U;
    if (previous == stamp.seconds) {
      ++counts.collided;
      continue;
    }
    previous = stamp.seconds;
    append_row(out, stamp.seconds, *value);
  }
  return {};
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

}  // namespace

namespace detail {

std::string imeds_name(std::string_view name, std::size_t index) {
  const std::string cleaned = underscored(name);
  return cleaned.empty() ? std::format("station_{}", index) : cleaned;
}

}  // namespace detail

std::expected<Read<std::string>, FormatError> format_imeds(
    const core::StationTable& table, std::string_view source) {
  if (table.schema().size() != 1) {
    return std::unexpected{
        FormatError{.code = FormatErrc::wrong_column_count,
                    .subject = std::to_string(table.schema().size())}};
  }
  std::string out = header_text(table.schema().front(), source);
  Counts counts;
  for (const core::StationIndex i : table.stations()) {
    if (auto done = append_station(out, table, i, counts); not done) {
      return std::unexpected{std::move(done).error()};
    }
  }
  std::vector<Warning> warnings;
  if (counts.floored > 0) {
    warnings.push_back({.code = WarningCode::time_precision_dropped,
                        .subject = {},
                        .count = counts.floored});
  }
  if (counts.collided > 0) {
    warnings.push_back({.code = WarningCode::duplicate_times_dropped,
                        .subject = {},
                        .count = counts.collided});
  }
  return Read<std::string>{.value = std::move(out),
                           .warnings = std::move(warnings)};
}

std::expected<std::vector<Warning>, Error> write_imeds(
    const std::filesystem::path& path, const core::StationTable& table,
    std::string_view source) {
  auto text = format_imeds(table, source);
  if (not text) {
    return std::unexpected{Error{std::move(text).error()}};
  }
  const std::string& bytes = text->value;
  const auto written = write_file_atomic(
      path, [&bytes](std::ostream& out) -> std::expected<void, FileError> {
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return {};
      });
  if (not written) {
    return std::unexpected{written.error()};
  }
  return std::move(text->warnings);
}

}  // namespace mov::io
