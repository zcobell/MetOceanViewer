// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target for parse_imeds (docs/core-design.md section 7.5). The
// input is the IMEDS text. The oracle:
//  - the parser never crashes, and an error names a line that exists (or the
//    one after the last, for a missing header) and keeps its context short;
//  - an accepted file formats (its times lie in the years 0000-9999 and it has
//    one column), and the text parses again without a normalization warning
//    to the same stations: ids as the writer's names make them, the same
//    value samples at the same times (Missing rows are not written), values,
//    latitudes and longitudes equal to six decimals (a longitude may wrap at
//    +-180), the same datum and unit.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

struct Rows {
  std::vector<core::Time> times;
  std::vector<double> values;
};

// The value samples of station i (what the writer prints).
Rows value_rows(const core::StationTable& t, core::StationIndex i) {
  Rows rows;
  const std::span<const core::Time> times = t.times(i);
  const std::span<const core::Sample> samples =
      t.column(i, core::ColumnIndex{0});
  for (std::size_t j = 0; j < times.size(); ++j) {
    if (const std::optional<double> v = samples[j].value()) {
      rows.times.push_back(times[j]);
      rows.values.push_back(*v);
    }
  }
  return rows;
}

void check_error(const io::ParseError& e, std::string_view text) {
  require(e.line() >= 1 and e.line() <= line_count(text) + 1);
  require(e.context().size() <= io::ParseError::max_context_bytes);
  // corrupt_record at line 0 is the parser's "cannot happen" branch.
  require(e.code() != io::ParseErrc::corrupt_record);
}

void check_station(const core::StationTable& a, const core::StationTable& b,
                   core::StationIndex i, const std::string& expected_id) {
  require(std::string{b.station(i).id.view()} == expected_id);
  require(close(a.station(i).location.lat(), b.station(i).location.lat()));
  require(close_longitude(a.station(i).location.lon(),
                          b.station(i).location.lon()));
  const Rows before = value_rows(a, i);
  const Rows after = value_rows(b, i);
  require(before.times == after.times);
  require(before.values.size() == after.values.size());
  for (std::size_t j = 0; j < before.values.size(); ++j) {
    require(close(before.values[j], after.values[j]));
  }
}

void check_round_trip(const io::ImedsFile& file) {
  const core::StationTable& table = file.table;
  const auto text = io::format_imeds(table);
  require(text.has_value());
  const auto again = io::parse_imeds(text->value);
  require(again.has_value());
  const core::StationTable& second = again->value.table;
  require(second.size() == table.size());

  std::vector<std::string> names;
  for (const core::StationIndex i : table.stations()) {
    names.push_back(
        io::detail::imeds_name(table.station(i).name.view(), i.value()));
  }
  const io::detail::UniqueIds expected = io::detail::uniquify_ids(names);
  for (const core::StationIndex i : table.stations()) {
    check_station(table, second, i, expected.ids[i.value()]);
  }
  for (const io::Warning& w : again->warnings) {
    require(w.code == io::WarningCode::duplicate_station_id_renamed or
            w.code == io::WarningCode::unrecognized_unit);
  }
  require(again->value.header.datum == file.header.datum);
  require(again->value.header.unit == file.header.unit);
  require(again->value.table.schema()[0].unit() == table.schema()[0].unit());
  require(again->value.table.schema()[0].datum() == table.schema()[0].datum());
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string text(data, data + size);
  const auto parsed = io::parse_imeds(text);
  if (not parsed) {
    check_error(parsed.error(), text);
    return 0;
  }
  require(parsed->value.table.schema().size() == 1);
  for (const core::StationIndex i : parsed->value.table.stations()) {
    require(parsed->value.table.times(i).size() ==
            parsed->value.table.column(i, core::ColumnIndex{0}).size());
  }
  check_round_trip(parsed->value);
  return 0;
}
