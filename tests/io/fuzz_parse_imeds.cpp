// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target for parse_imeds (docs/core-design.md section 7.5). The
// input is the IMEDS text. The oracle:
//  - the parser never crashes; its error is a ParseError that names a line
//    that exists (or the one after the last), with a short context, and is
//    never the internal `corrupt_record` at line 0;
//  - masking is conserved: the samples that are not values are exactly the
//    ones the warnings count (legacy sentinels and non-finite tokens);
//  - an accepted file formats (its times lie in the years 0000-9999 and it
//    has one column), and the text parses again to the same stations: ids as
//    the writer's names make them, the same value samples at the same times
//    (Missing rows are not written; a value whose text reads as a sentinel
//    is written and comes back Missing, as `value_reads_as_missing` says),
//    values, latitudes and longitudes equal to six decimals (a longitude may
//    wrap at +-180), the same datum and unit, and only the warnings the text
//    still earns;
//  - the writer is idempotent: format(parse(format(T))) == format(T) byte for
//    byte, when nothing is lost to a sentinel and no number needs more than
//    15 digits (beyond that a six-decimal text need not parse back to the
//    double that printed it).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/sample.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/io/detail/station_names.hpp"
#include "mov/io/imeds.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

namespace core = mov::core;
namespace io = mov::io;

[[noreturn]] void fail() { std::abort(); }

void require(bool ok) {
  if (not ok) {
    fail();
  }
}

std::size_t line_count(std::string_view text) {
  return static_cast<std::size_t>(std::ranges::count(text, '\n')) + 1;
}

// The decimal text of a six-decimal number is within 5e-7 of it, and the
// double it parses to within a few ulp of that.
bool close(double a, double b) {
  return std::abs(a - b) <= 5.1e-7 + 3e-16 * std::abs(a);
}

bool close_longitude(double a, double b) {
  const double diff = std::abs(a - b);
  return close(a, b) or close(diff, 360.0);
}

// Below this a six-decimal text has at most 15 significant digits, so it
// parses to the double that printed it.
constexpr double exact_below = 1e9;

struct Rows {
  std::vector<core::Time> times;
  std::vector<double> values;
};

// The value samples of station i that a reader keeps: those whose written text
// is not a masked sentinel.
Rows kept_rows(const core::StationTable& t, core::StationIndex i,
               bool drop_masked_text) {
  Rows rows;
  const std::span<const core::Time> times = t.times(i);
  const std::span<const core::Sample> samples =
      t.column(i, core::ColumnIndex{0});
  for (std::size_t j = 0; j < times.size(); ++j) {
    const std::optional<double> v = samples[j].value();
    if (v and not(drop_masked_text and io::detail::reads_as_missing(*v))) {
      rows.times.push_back(times[j]);
      rows.values.push_back(*v);
    }
  }
  return rows;
}

std::size_t value_count(const core::StationTable& t) {
  std::size_t n = 0;
  for (const core::StationIndex i : t.stations()) {
    n += kept_rows(t, i, false).values.size();
  }
  return n;
}

std::size_t warned(const std::vector<io::Warning>& warnings,
                   io::WarningCode code) {
  return std::accumulate(warnings.begin(), warnings.end(), std::size_t{0},
                         [code](std::size_t n, const io::Warning& w) {
                           return n + (w.code == code ? w.count : 0);
                         });
}

void check_error(const io::Error& error, std::string_view text) {
  const auto* e = std::get_if<io::ParseError>(&error);
  require(e != nullptr);
  require(e->line() >= 1 and e->line() <= line_count(text) + 1);
  require(e->context().size() <= io::ParseError::max_context_bytes);
  // corrupt_record at line 0 is the parser's "cannot happen" branch.
  require(e->code() != io::ParseErrc::corrupt_record);
}

void check_station(const core::StationTable& a, const core::StationTable& b,
                   core::StationIndex i, const std::string& expected_id) {
  require(std::string{b.station(i).id.view()} == expected_id);
  require(close(a.station(i).location.lat(), b.station(i).location.lat()));
  require(close_longitude(a.station(i).location.lon(),
                          b.station(i).location.lon()));
  const Rows before = kept_rows(a, i, true);
  const Rows after = kept_rows(b, i, false);
  require(before.times == after.times);
  require(before.values.size() == after.values.size());
  for (std::size_t j = 0; j < before.values.size(); ++j) {
    require(close(before.values[j], after.values[j]));
  }
}

bool all_small(const core::StationTable& t) {
  for (const core::StationIndex i : t.stations()) {
    const Rows rows = kept_rows(t, i, false);
    if (std::ranges::any_of(
            rows.values, [](double v) { return std::abs(v) >= exact_below; })) {
      return false;
    }
  }
  return true;
}

void check_second_parse(const io::ImedsFile& file, const io::ImedsFile& second,
                        std::size_t masked_by_text,
                        const std::vector<io::Warning>& warnings) {
  const core::StationTable& table = file.table;
  require(second.table.size() == table.size());
  std::vector<std::string> names;
  for (const core::StationIndex i : table.stations()) {
    names.push_back(
        io::detail::imeds_name(table.station(i).name.view(), i.value()));
  }
  const io::detail::UniqueIds expected = io::detail::uniquify_ids(names);
  for (const core::StationIndex i : table.stations()) {
    require(std::string{second.table.station(i).name.view()} ==
            names[i.value()]);
    check_station(table, second.table, i, expected.ids[i.value()]);
  }
  require(value_count(second.table) + masked_by_text == value_count(table));
  require(second.header.datum == file.header.datum);
  require(second.header.unit == file.header.unit);
  require(second.table.schema()[0].unit() == table.schema()[0].unit());
  require(second.table.schema()[0].datum() == table.schema()[0].datum());
  for (const io::Warning& w : warnings) {
    require(w.code == io::WarningCode::duplicate_station_id_renamed or
            w.code == io::WarningCode::unrecognized_unit or
            w.code == io::WarningCode::empty_station or
            w.code == io::WarningCode::legacy_sentinel_masked);
  }
  require(warned(warnings, io::WarningCode::legacy_sentinel_masked) ==
          masked_by_text);
}

void check_round_trip(const io::ImedsFile& file) {
  const auto text = io::format_imeds(file.table);
  require(text.has_value());
  const std::size_t masked_by_text =
      warned(text->warnings, io::WarningCode::value_reads_as_missing);
  const auto again = io::parse_imeds(text->value, io::ReadContext{});
  require(again.has_value());
  check_second_parse(file, again->value, masked_by_text, again->warnings);

  if (masked_by_text == 0 and all_small(file.table)) {
    const auto twice = io::format_imeds(again->value.table);
    require(twice.has_value());
    require(twice->value == text->value);
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string text(data, data + size);
  const auto parsed = io::parse_imeds(text, io::ReadContext{});
  if (not parsed) {
    check_error(parsed.error(), text);
    return 0;
  }
  const core::StationTable& table = parsed->value.table;
  require(table.schema().size() == 1);
  std::size_t samples = 0;
  for (const core::StationIndex i : table.stations()) {
    require(table.times(i).size() ==
            table.column(i, core::ColumnIndex{0}).size());
    samples += table.times(i).size();
  }
  // Every sample that is not a value was masked, and the warnings say so.
  const std::size_t masked =
      warned(parsed->warnings, io::WarningCode::legacy_sentinel_masked) +
      warned(parsed->warnings, io::WarningCode::nonfinite_masked);
  const std::size_t dropped =
      warned(parsed->warnings, io::WarningCode::duplicate_times_dropped);
  // (rows dropped as duplicates may have been masked ones: only a bound.)
  require(samples - value_count(table) <= masked);
  require(samples + dropped >= masked or masked == 0);
  check_round_trip(parsed->value);
  return 0;
}
