// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Series JSON (docs/series-json.md), streamed station by station.

#include "mov/io/series_json.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <ios>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "mov/core/datum.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/core/utf8.hpp"
#include "mov/io/detail/json_write.hpp"
#include "mov/io/error.hpp"
#include "mov/io/text_file.hpp"

namespace mov::io {

namespace {

namespace jt = detail::json_text;

constexpr std::size_t chunk_bytes = std::size_t{1} << 16;

// The text, built in a buffer and handed to `flush` whenever it holds at
// least `chunk` bytes, and at the end. flush takes the text out of the buffer
// and returns false to stop (a failed stream).
template <class Flush>
class Emitter {
 public:
  Emitter(std::size_t chunk, Flush flush)
      : chunk_{chunk}, flush_{std::move(flush)} {}

  [[nodiscard]] std::string& out() noexcept { return buffer_; }

  // Hands over a full chunk; false once the sink failed.
  [[nodiscard]] bool spill() {
    if (ok_ and buffer_.size() >= chunk_) {
      ok_ = flush_(buffer_);
    }
    return ok_;
  }

  void finish() {
    if (ok_) {
      ok_ = flush_(buffer_);
    }
  }

 private:
  std::size_t chunk_;
  Flush flush_;
  std::string buffer_;
  bool ok_ = true;
};

void member(std::string& out, std::string_view name) {
  jt::append_string(out, name);
  out.push_back(':');
}

// Text the format writes from a series' metadata: UTF-8 without NUL, else
// `bad_encoding` about the quantity (StationText guarantees the rest).
std::expected<void, FormatError> check_text(const core::SeriesMeta& meta) {
  const auto writable = [](std::string_view text) {
    return core::is_valid_utf8(text) and
           text.find('\0') == std::string_view::npos;
  };
  const std::optional<core::Unit>& unit = meta.unit();
  if (not writable(meta.label()) or
      (unit and not writable(core::udunits(*unit)))) {
    return std::unexpected{
        FormatError{.code = FormatErrc::bad_encoding,
                    .subject = std::string{core::token(meta.quantity())}}};
  }
  return {};
}

// {"id":…,"name":…,"provider":…,"lon":…,"lat":…,
void open_station(std::string& out, const core::FileStation& station,
                  bool first) {
  out += first ? "\n{" : ",\n{";
  member(out, "id");
  jt::append_string(out, station.id.view());
  out.push_back(',');
  member(out, "name");
  jt::append_string(out, station.name.view());
  if (station.source) {
    out.push_back(',');
    member(out, "provider");
    jt::append_string(out, core::to_token(*station.source));
  }
  out.push_back(',');
  member(out, "lon");
  jt::append_shortest(out, station.location.lon());
  out.push_back(',');
  member(out, "lat");
  jt::append_shortest(out, station.location.lat());
  out.push_back(',');
}

// {"quantity":…,"label":…,"unit":…,"datum":…,"sublocation":…,
void open_series(std::string& out, const core::SeriesMeta& meta,
                 const core::usgs::Sublocation* sublocation, bool first) {
  out += first ? "{" : ",{";
  member(out, "quantity");
  jt::append_string(out, core::token(meta.quantity()));
  if (not meta.label().empty()) {
    out.push_back(',');
    member(out, "label");
    jt::append_string(out, meta.label());
  }
  if (const auto& unit = meta.unit()) {
    out.push_back(',');
    member(out, "unit");
    jt::append_string(out, core::udunits(*unit));
  }
  if (const auto datum = meta.datum()) {
    out.push_back(',');
    member(out, "datum");
    jt::append_string(out, core::to_string(*datum));
  }
  if (sublocation != nullptr) {
    out.push_back(',');
    member(out, "sublocation");
    jt::append_string(out, sublocation->view());
  }
  out.push_back(',');
}

// The sublocation of each (station, column) that has one.
using Sublocations = std::map<std::pair<std::size_t, std::size_t>,
                              const core::usgs::Sublocation*>;

std::expected<Sublocations, FormatError> sublocations_of(
    const core::StationTable& table, std::span<const ColumnSublocation> given) {
  Sublocations by_column;
  for (const ColumnSublocation& s : given) {
    const std::pair key{s.station.value(), s.column.value()};
    if (key.first >= table.size() or key.second >= table.schema().size() or
        not by_column.emplace(key, &s.sublocation).second) {
      return std::unexpected{FormatError{.code = FormatErrc::bad_option,
                                         .subject = "sublocation"}};
    }
  }
  return by_column;
}

std::string_view status_of(const core::Sample& s) {
  if (s.is_value()) {
    return "value";
  }
  return s.is_dry() ? "dry" : "missing";
}

// `"<name>":[a,b,...]` of each item's text, spilling as it goes.
template <class Flush, class Range, class Append>
[[nodiscard]] bool append_array(Emitter<Flush>& e, std::string_view name,
                                const Range& items, Append append) {
  member(e.out(), name);
  e.out().push_back('[');
  bool first = true;
  for (const auto& item : items) {
    if (not first) {
      e.out().push_back(',');
    }
    first = false;
    append(e.out(), item);
    if (not e.spill()) {
      return false;
    }
  }
  e.out().push_back(']');
  return true;
}

// "value":[…],"status":[…]? and the closing brace.
template <class Flush>
[[nodiscard]] bool close_series(Emitter<Flush>& e,
                                std::span<const core::Sample> samples) {
  if (not append_array(e, "value", samples,
                       [](std::string& out, const core::Sample& s) {
                         if (const auto v = s.value()) {
                           jt::append_shortest(out, *v);
                         } else {
                           out += "null";
                         }
                       })) {
    return false;
  }
  if (std::ranges::any_of(samples, &core::Sample::is_dry)) {
    e.out().push_back(',');
    if (not append_array(e, "status", samples,
                         [](std::string& out, const core::Sample& s) {
                           jt::append_string(out, status_of(s));
                         })) {
      return false;
    }
  }
  e.out().push_back('}');
  return true;
}

void open_document(std::string& out) {
  out += "{";
  member(out, "format");
  jt::append_string(out, series_format);
  out += ",\"version\":\"1.0\",\n \"stations\":[";
}

void close_document(std::string& out, bool empty) {
  out += empty ? "]}\n" : "\n]}\n";
}

// Each station's axis once, then its columns: "time":[…],"series":[…]}.
template <class Flush>
void emit(const core::StationTable& table, const Sublocations& sublocations,
          Emitter<Flush>& e) {
  open_document(e.out());
  for (const core::StationIndex i : table.stations()) {
    open_station(e.out(), table.station(i), i.value() == 0);
    if (not append_array(e, "time", table.times(i),
                         [](std::string& out, core::Time t) {
                           jt::append_timestamp_ms(out, t);
                         })) {
      return;
    }
    e.out() += ",\"series\":[";
    for (const std::size_t k :
         std::views::iota(std::size_t{0}, table.schema().size())) {
      const auto sublocation = sublocations.find({i.value(), k});
      open_series(
          e.out(), table.schema()[k],
          sublocation == sublocations.end() ? nullptr : sublocation->second,
          k == 0);
      if (not close_series(e, table.column(i, core::ColumnIndex{k}))) {
        return;
      }
    }
    e.out() += "]}";
  }
  close_document(e.out(), table.size() == 0);
  e.finish();
}

// Each station's days, then its one series with its statistic:
// "date":[…],"series":[{…,"statistic":…,"value":[…]}]}.
template <class Flush>
void emit(std::span<const DailyAtStation> stations, Emitter<Flush>& e) {
  open_document(e.out());
  bool first = true;
  for (const DailyAtStation& s : stations) {
    open_station(e.out(), s.station, first);
    first = false;
    if (not append_array(
            e, "date", s.data.days(),
            [](std::string& out, std::chrono::local_days d) {
              jt::append_date(out, std::chrono::sys_days{d.time_since_epoch()});
            })) {
      return;
    }
    e.out() += ",\"series\":[";
    open_series(e.out(), s.data.meta(),
                s.sublocation ? &*s.sublocation : nullptr, true);
    member(e.out(), "statistic");
    const core::usgs::StatisticId statistic = s.data.statistic();
    jt::append_string(e.out(), statistic.view());
    e.out().push_back(',');
    if (not close_series(e, s.data.samples())) {
      return;
    }
    e.out() += "]}";
  }
  close_document(e.out(), stations.empty());
  e.finish();
}

// What a table's text needs: its metadata texts checked, its sublocations
// by column.
std::expected<Sublocations, FormatError> check(
    const core::StationTable& table,
    std::span<const ColumnSublocation> sublocations) {
  for (const core::SeriesMeta& meta : table.schema()) {
    if (auto ok = check_text(meta); not ok) {
      return std::unexpected{std::move(ok).error()};
    }
  }
  return sublocations_of(table, sublocations);
}

std::expected<void, FormatError> check(
    std::span<const DailyAtStation> stations) {
  for (const DailyAtStation& s : stations) {
    if (auto ok = check_text(s.data.meta()); not ok) {
      return ok;
    }
  }
  return {};
}

// The text of the whole input in one buffer (`emit_into(e)` writes it).
template <class Emit>
std::string format_whole(Emit emit_into) {
  std::string text;
  // One chunk: the buffer is the result.
  auto take = [&text](std::string& buffer) {
    text = std::move(buffer);
    buffer.clear();
    return true;
  };
  Emitter e{std::numeric_limits<std::size_t>::max(), take};
  emit_into(e);
  return text;
}

// The text streamed into an atomic write in chunks.
template <class Emit>
std::expected<void, Error> write_chunked(const std::filesystem::path& path,
                                         Emit emit_into) {
  return write_file_atomic(
      path, [&emit_into](std::ostream& out) -> std::expected<void, FileError> {
        Emitter e{chunk_bytes, [&out](std::string& buffer) {
                    out.write(buffer.data(),
                              static_cast<std::streamsize>(buffer.size()));
                    buffer.clear();
                    return static_cast<bool>(out);  // a full disk stops it
                  }};
        emit_into(e);
        return {};
      });
}

}  // namespace

std::expected<std::string, FormatError> format_series_json(
    const core::StationTable& table,
    std::span<const ColumnSublocation> sublocations) {
  return check(table, sublocations).transform([&table](const Sublocations& by) {
    return format_whole([&](auto& e) { emit(table, by, e); });
  });
}

std::expected<std::string, FormatError> format_series_json(
    std::span<const DailyAtStation> stations) {
  return check(stations).transform([stations] {
    return format_whole([stations](auto& e) { emit(stations, e); });
  });
}

std::expected<void, Error> write_series_json(
    const std::filesystem::path& path, const core::StationTable& table,
    std::span<const ColumnSublocation> sublocations) {
  auto by = check(table, sublocations);
  if (not by) {
    return std::unexpected{Error{std::move(by).error()}};
  }
  return write_chunked(path, [&](auto& e) { emit(table, *by, e); });
}

std::expected<void, Error> write_series_json(
    const std::filesystem::path& path,
    std::span<const DailyAtStation> stations) {
  if (auto ok = check(stations); not ok) {
    return std::unexpected{Error{std::move(ok).error()}};
  }
  return write_chunked(path, [stations](auto& e) { emit(stations, e); });
}

}  // namespace mov::io
