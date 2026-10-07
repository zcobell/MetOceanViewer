// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: parse_adcirc_ascii, with the table assembly and a fixed
// cold start, must never crash, and a table it returns is consistent:
//  - every column is as long as the shared axis, and the axis is strictly
//    increasing and within +-2^53 ms;
//  - the stations are the selected ones, in the selection's order;
//  - Dry appears only in elevation output;
//  - the warnings are in range (at most one dropped record, counts that add
//  up);
//  - an error is a ParseError or FormatError that points into the text;
//  - the selection is a view of the whole: when the file parses with every
//    station selected, a parse with a subset succeeds and holds exactly those
//    stations' columns on the same axis.
// Input: byte 0 the output kind, byte 1 the number of stations (0..6), byte 2
// the selection (bit i picks station i; bit 7 reverses the order), then the
// text of the output file.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/geo.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/io/adcirc_ascii.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

namespace core = mov::core;
namespace io = mov::io;

[[noreturn]] void fail() { std::abort(); }

// Bits 0..6 of the selection byte pick stations; bit 7 reverses the order.
constexpr std::size_t station_limit = 8;

std::vector<core::FileStation> make_stations(std::size_t n) {
  std::vector<core::FileStation> stations;
  const auto where = core::Location::make({.lat = 29.0, .lon = -90.0});
  if (not where) {
    fail();
  }
  for (std::size_t i = 0; i < n; ++i) {
    auto key = core::StationKey::make(std::to_string(i));
    if (not key) {
      fail();
    }
    stations.push_back({.id = *std::move(key),
                        .name = {},
                        .location = *where,
                        .native = std::nullopt,
                        .source = core::DataSource::adcirc});
  }
  return stations;
}

core::Time cold_start() {
  using namespace std::chrono;
  return time_point_cast<milliseconds>(sys_days{year{2010} / January / 1});
}

io::AdcircKind kind_of(std::uint8_t byte) {
  switch (byte % 4) {
    case 0:
      return io::AdcircKind::elevation;
    case 1:
      return io::AdcircKind::velocity;
    case 2:
      return io::AdcircKind::pressure;
    default:
      return io::AdcircKind::wind;
  }
}

std::vector<std::size_t> selected(std::uint8_t mask, std::size_t n) {
  std::vector<std::size_t> picked;
  for (std::size_t i = 0; i < n; ++i) {
    if (((mask >> i) & 1U) != 0) {
      picked.push_back(i);
    }
  }
  if ((mask & 0x80U) != 0) {
    std::ranges::reverse(picked);
  }
  return picked;
}

void check_error(const io::Error& error, std::string_view text) {
  if (const auto* parse = std::get_if<io::ParseError>(&error)) {
    if (parse->context().size() > io::ParseError::max_context_bytes) {
      fail();
    }
    const auto lines =
        static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
    if (parse->line() > lines + 2U) {
      fail();
    }
    return;
  }
  if (not std::holds_alternative<io::FormatError>(error)) {
    fail();  // nothing else can come out of a text parse
  }
}

void check_table(const core::StationTable& table, io::AdcircKind kind,
                 std::span<const std::size_t> picked) {
  if (table.size() != picked.size() or
      table.schema().size() != io::column_count(kind)) {
    fail();
  }
  for (const core::StationIndex i : table.stations()) {
    if (table.station(i).id.view() != std::to_string(picked[i.value()])) {
      fail();
    }
    const auto times = table.times(i);
    for (std::size_t t = 0; t < times.size(); ++t) {
      const std::int64_t ms = times[t].time_since_epoch().count();
      if (ms > core::max_abs_time_ms or ms < -core::max_abs_time_ms or
          (t > 0 and not(times[t - 1] < times[t]))) {
        fail();
      }
    }
    for (std::size_t k = 0; k < table.schema().size(); ++k) {
      const auto cells = table.column(i, core::ColumnIndex{k});
      if (cells.size() != times.size()) {
        fail();
      }
      if (kind != io::AdcircKind::elevation and
          std::ranges::any_of(cells,
                              [](core::Sample s) { return s.is_dry(); })) {
        fail();
      }
    }
  }
}

void check_warnings(const std::vector<io::Warning>& warnings) {
  for (const io::Warning& w : warnings) {
    if (w.count == 0) {
      fail();
    }
    if (w.code == io::WarningCode::partial_record_dropped and w.count != 1) {
      fail();
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  constexpr std::size_t prefix = 3;
  if (size < prefix) {
    return 0;
  }
  const io::AdcircKind kind = kind_of(data[0]);
  const std::size_t n = data[1] % (station_limit - 1);
  const std::vector<std::size_t> picked = selected(data[2], n);
  const std::string storage(data + prefix, data + size);
  const std::string_view text{storage};
  const auto stations = make_stations(n);

  auto selection = core::StationSelection::make(picked, n);
  if (not selection) {
    fail();  // distinct indices below n by construction
  }
  const io::ReadContext ctx{};
  const auto subset = io::parse_adcirc_ascii(
      text, stations,
      {.kind = kind, .cold_start = cold_start(), .stations = *selection}, ctx);
  if (not subset) {
    check_error(subset.error(), text);
  } else {
    check_table(subset->value, kind, picked);
    check_warnings(subset->warnings);
  }

  // The subset is a view of the whole.
  const auto everything =
      io::parse_adcirc_ascii(text, stations,
                             {.kind = kind,
                              .cold_start = cold_start(),
                              .stations = core::StationSelection::all(n)},
                             ctx);
  if (not everything) {
    check_error(everything.error(), text);
    return 0;
  }
  std::vector<std::size_t> all(n);
  for (std::size_t i = 0; i < n; ++i) {
    all[i] = i;
  }
  check_table(everything->value, kind, all);
  if (not subset) {
    fail();  // the whole parsed, so any part must
  }
  // A damaged line of an unselected station can end the whole's last record
  // (a dropped partial record) where the subset, which never reads that line,
  // keeps one more. The records they share are the same.
  const auto warned = [](const io::Read<core::StationTable>& r,
                         io::WarningCode code) {
    return std::ranges::any_of(
        r.warnings, [code](const io::Warning& w) { return w.code == code; });
  };
  const bool whole_dropped_a_record =
      warned(*everything, io::WarningCode::partial_record_dropped);
  const bool reordered =
      warned(*everything, io::WarningCode::times_reordered) or
      warned(*everything, io::WarningCode::duplicate_times_dropped) or
      warned(*subset, io::WarningCode::times_reordered) or
      warned(*subset, io::WarningCode::duplicate_times_dropped);
  if (whole_dropped_a_record and reordered) {
    return 0;  // the extra record may land anywhere in the sorted axis
  }
  for (std::size_t p = 0; p < picked.size(); ++p) {
    const core::StationIndex mine{p};
    const core::StationIndex theirs{picked[p]};
    const auto whole_times = everything->value.times(theirs);
    const auto part_times = subset->value.times(mine);
    const std::size_t extra = whole_dropped_a_record ? 1U : 0U;
    if (part_times.size() < whole_times.size() or
        part_times.size() > whole_times.size() + extra or
        not std::ranges::equal(part_times.first(whole_times.size()),
                               whole_times)) {
      fail();
    }
    for (std::size_t k = 0; k < subset->value.schema().size(); ++k) {
      const auto part = subset->value.column(mine, core::ColumnIndex{k});
      if (not std::ranges::equal(
              part.first(whole_times.size()),
              everything->value.column(theirs, core::ColumnIndex{k}))) {
        fail();
      }
    }
  }
  return 0;
}
