// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Reading a station catalog (docs/station-catalog.md). The stations are
// streamed through the SAX reader (mov/io/detail/json.hpp) and decoded one
// at a time; the members that say what the file is (format, version,
// provider) may come after them, so a station's failure is kept and
// reported only after those are checked (the order of section 8.3).

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/ascii.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/enum_set.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/overloaded.hpp"
#include "mov/core/provider/capabilities.hpp"
#include "mov/core/provider/coops.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/io/detail/json.hpp"
#include "mov/io/detail/json_write.hpp"
#include "mov/io/detail/reporting.hpp"
#include "mov/io/detail/station_catalog_text.hpp"
#include "mov/io/detail/text_position.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/station_catalog.hpp"
#include "mov/io/text_file.hpp"
#include "mov/io/warning.hpp"

namespace mov::io {

namespace {

namespace json = detail::json;
namespace tokens = detail::catalog;
using core::provider::Coops;
using core::provider::Ndbc;
using core::provider::Usgs;
using json::Field;
using json::Keep;
using json::Value;

// ---- what is kept
// ------------------------------------------------------------

constexpr std::size_t max_sources = 256;
constexpr std::size_t max_tokens = 64;
constexpr std::size_t max_series = 4096;

constexpr Keep tokens_keep{.element = &json::leaf, .max_elements = max_tokens};

constexpr std::array source_fields{
    Field{.name = "url", .keep = &json::leaf},
    Field{.name = "retrieved", .keep = &json::leaf}};
constexpr Keep source_keep{.fields = source_fields, .unknown_names = true};
constexpr Keep sources_keep{.element = &source_keep,
                            .max_elements = max_sources};

// Every datum token names a member of `datums`.
constexpr auto datum_fields = [] {
  const auto& all = core::Enumerators<core::VerticalDatum>::values;
  std::array<Field, std::tuple_size_v<std::remove_cvref_t<decltype(all)>>>
      fields{};
  for (std::size_t i = 0; i < fields.size(); ++i) {
    fields[i] = Field{.name = core::to_string(all[i]), .keep = &json::leaf};
  }
  return fields;
}();
constexpr Keep datums_keep{.fields = datum_fields, .unknown_names = true};

constexpr std::array coops_fields{
    Field{.name = "observed", .keep = &tokens_keep},
    Field{.name = "met", .keep = &tokens_keep},
    Field{.name = "air_gap", .keep = &json::leaf},
    Field{.name = "predictions", .keep = &json::leaf},
    Field{.name = "datums_offered", .keep = &tokens_keep},
    Field{.name = "datums", .keep = &datums_keep}};
constexpr Keep coops_keep{.fields = coops_fields, .unknown_names = true};

constexpr std::array series_fields{
    Field{.name = "id", .keep = &json::leaf},
    Field{.name = "parameter", .keep = &json::leaf},
    Field{.name = "statistic", .keep = &json::leaf},
    Field{.name = "parameter_name", .keep = &json::leaf},
    Field{.name = "primary", .keep = &json::leaf},
    Field{.name = "unit", .keep = &json::leaf},
    Field{.name = "first", .keep = &json::leaf},
    Field{.name = "last", .keep = &json::leaf}};
constexpr Keep series_keep{.fields = series_fields, .unknown_names = true};
constexpr Keep series_list_keep{.element = &series_keep,
                                .max_elements = max_series};
constexpr std::array usgs_fields{
    Field{.name = "series", .keep = &series_list_keep}};
constexpr Keep usgs_keep{.fields = usgs_fields, .unknown_names = true};

constexpr std::array ndbc_fields{
    Field{.name = "owner", .keep = &json::leaf},
    Field{.name = "program", .keep = &json::leaf},
    Field{.name = "platform_type", .keep = &json::leaf},
    Field{.name = "elevation_m", .keep = &json::leaf}};
constexpr Keep ndbc_keep{.fields = ndbc_fields, .unknown_names = true};

constexpr std::array station_fields{Field{.name = "id", .keep = &json::leaf},
                                    Field{.name = "name", .keep = &json::leaf},
                                    Field{.name = "lon", .keep = &json::leaf},
                                    Field{.name = "lat", .keep = &json::leaf},
                                    Field{.name = "first", .keep = &json::leaf},
                                    Field{.name = "last", .keep = &json::leaf},
                                    Field{.name = "coops", .keep = &coops_keep},
                                    Field{.name = "usgs", .keep = &usgs_keep},
                                    Field{.name = "ndbc", .keep = &ndbc_keep}};
constexpr Keep station_keep{.fields = station_fields, .unknown_names = true};
constexpr Keep stations_keep{.element = &station_keep};

constexpr std::array document_fields{
    Field{.name = "format", .keep = &json::leaf},
    Field{.name = "version", .keep = &json::leaf},
    Field{.name = "provider", .keep = &json::leaf},
    Field{.name = "generated", .keep = &json::leaf},
    Field{.name = "sources", .keep = &sources_keep},
    Field{.name = "stations", .keep = &stations_keep, .streamed = true}};
constexpr Keep document_keep{.fields = document_fields, .unknown_names = true};

// What a station costs against ReadLimits::max_elements (as a CO-OPS list
// station, core-design.md section 5), and each USGS series besides.
constexpr std::size_t elements_per_station = 16;

// ---- failures and unknown content
// ------------------------------------------

// A failure and where it is in the body: the first in document order is the
// one reported.
struct Fault {
  std::size_t offset;
  Error error;
};

template <class T>
using Decoded = std::expected<T, Fault>;

// Members and tokens the reader does not know, by where they are. In a file
// of this minor or an older one the first is an error; in a newer one each
// path is a warning with its count.
class Unknowns {
 public:
  static constexpr std::size_t max_paths = 32;

  void add(std::string path, std::size_t offset) {
    first_ = std::min(first_.value_or(offset), offset);
    if (const auto found = counts_.find(path); found != counts_.end()) {
      ++found->second;
    } else if (counts_.size() < max_paths) {
      counts_.emplace(std::move(path), 1);
    } else {
      ++other_;
    }
  }

  [[nodiscard]] std::optional<std::size_t> first() const noexcept {
    return first_;
  }

  void report(std::vector<Warning>& warnings) const {
    for (const auto& [path, count] : counts_) {
      warnings.push_back({.code = WarningCode::newer_content_ignored,
                          .subject = path,
                          .count = count});
    }
    append_if_counted(warnings, {.code = WarningCode::newer_content_ignored,
                                 .subject = "other",
                                 .count = other_});
  }

 private:
  std::map<std::string, std::size_t, std::less<>> counts_;
  std::size_t other_ = 0;
  std::optional<std::size_t> first_;
};

// One decoded station of whichever provider its block names.
using AnyEntry =
    std::variant<CatalogEntry<Coops>, CatalogEntry<Usgs>, CatalogEntry<Ndbc>>;

class Decoder {
 public:
  explicit Decoder(std::string_view body) : body_{body} {}

  [[nodiscard]] Unknowns& unknowns() noexcept { return unknowns_; }

  [[nodiscard]] Fault fault(ParseErrc code, std::size_t offset) const {
    return Fault{.offset = offset,
                 .error = Error{json::error_at(code, body_, offset)}};
  }
  [[nodiscard]] Fault fault(ParseErrc code, const Value& at) const {
    return fault(code, at.offset);
  }

  // Records the members of `object` that `keep` does not name.
  void note_unknown(const Value& object, const Keep& keep,
                    std::string_view path) {
    const json::Object* members = json::as_object(object);
    if (members == nullptr) {
      return;
    }
    for (const json::Member& m : *members) {
      if (std::ranges::find(keep.fields, std::string_view{m.name},
                            &Field::name) == keep.fields.end()) {
        const std::string name = detail::subject_of(m.name);
        unknowns_.add(path.empty() ? name : std::string{path} + "." + name,
                      m.key_offset);
      }
    }
  }

  // ---- members ----

  [[nodiscard]] Decoded<const Value*> required(const Value& object,
                                               std::string_view name) const {
    const Value* v = json::member(object, name);
    if (v == nullptr) {
      return std::unexpected{fault(ParseErrc::bad_member, object)};
    }
    return v;
  }

  [[nodiscard]] Decoded<const json::Object*> object_of(const Value& v) const {
    const json::Object* o = json::as_object(v);
    if (o == nullptr) {
      return std::unexpected{fault(ParseErrc::bad_member, v)};
    }
    return o;
  }

  [[nodiscard]] Decoded<const json::Array*> array_of(const Value& v) const {
    const json::Array* a = json::as_array(v);
    if (a == nullptr) {
      return std::unexpected{fault(ParseErrc::bad_member, v)};
    }
    return a;
  }

  [[nodiscard]] Decoded<std::string_view> string_of(const Value& v) const {
    const std::string* s = json::as_string(v);
    if (s == nullptr) {
      return std::unexpected{fault(ParseErrc::bad_member, v)};
    }
    return std::string_view{*s};
  }

  [[nodiscard]] Decoded<double> number_of(const Value& v) const {
    const std::optional<double> d = json::as_double(v);
    if (not d) {
      return std::unexpected{fault(ParseErrc::bad_member, v)};
    }
    return *d;
  }

  [[nodiscard]] Decoded<bool> bool_of(const Value& v) const {
    const bool* b = std::get_if<bool>(&v.data);
    if (b == nullptr) {
      return std::unexpected{fault(ParseErrc::bad_member, v)};
    }
    return *b;
  }

  [[nodiscard]] Decoded<core::StationText> text_of(const Value& v) const {
    return string_of(v).and_then(
        [&](std::string_view s) -> Decoded<core::StationText> {
          auto t = core::StationText::make(std::string{s});
          if (not t) {
            return std::unexpected{fault(ParseErrc::bad_member, v)};
          }
          return *std::move(t);
        });
  }

  // An optional text member: absent is empty text.
  [[nodiscard]] Decoded<core::StationText> optional_text(
      const Value& object, std::string_view name) const {
    const Value* v = json::member(object, name);
    if (v == nullptr) {
      return core::StationText{};
    }
    return text_of(*v);
  }

  [[nodiscard]] Decoded<std::chrono::sys_days> date_of(const Value& v) const {
    return string_of(v).and_then(
        [&](std::string_view s) -> Decoded<std::chrono::sys_days> {
          const auto day = detail::json_text::parse_date(s);
          if (not day) {
            return std::unexpected{fault(ParseErrc::bad_date, v)};
          }
          return *day;
        });
  }

  [[nodiscard]] Decoded<core::Time> timestamp_of(const Value& v) const {
    return string_of(v).and_then(
        [&](std::string_view s) -> Decoded<core::Time> {
          const auto t = detail::json_text::parse_timestamp(s);
          if (not t) {
            return std::unexpected{fault(ParseErrc::bad_date, v)};
          }
          return *t;
        });
  }

  // `first` and `last` of an object, either absent.
  [[nodiscard]] Decoded<core::ValidRange> validity_of(
      const Value& object) const {
    std::optional<std::chrono::sys_days> first;
    std::optional<std::chrono::sys_days> last;
    if (const Value* v = json::member(object, "first")) {
      auto day = date_of(*v);
      if (not day) {
        return std::unexpected{std::move(day).error()};
      }
      first = *day;
    }
    const Value* last_value = json::member(object, "last");
    if (last_value != nullptr) {
      auto day = date_of(*last_value);
      if (not day) {
        return std::unexpected{std::move(day).error()};
      }
      last = *day;
    }
    const auto range = core::ValidRange::make({.first = first, .last = last});
    if (not range) {
      return std::unexpected{fault(ParseErrc::out_of_range, *last_value)};
    }
    return *range;
  }

  // A set of tokens: each a string, none repeated. An unknown token is
  // recorded (`<path>[]=<token>`) and left out.
  template <class E, std::size_t N>
  [[nodiscard]] Decoded<core::EnumSet<E>> set_of(
      const Value& array, const std::array<tokens::TokenRow<E>, N>& rows,
      std::string_view path) {
    return set_by(
        array, [&](std::string_view t) { return tokens::value_of(rows, t); },
        path);
  }

  template <class Lookup>
  [[nodiscard]] auto set_by(const Value& array, Lookup lookup,
                            std::string_view path)
      -> Decoded<core::EnumSet<typename std::invoke_result_t<
          Lookup, std::string_view>::value_type>> {
    using E =
        typename std::invoke_result_t<Lookup, std::string_view>::value_type;
    const auto elements = array_of(array);
    if (not elements) {
      return std::unexpected{elements.error()};
    }
    core::EnumSet<E> set;
    std::vector<std::string_view> seen;
    for (const Value& element : **elements) {
      const auto token = string_of(element);
      if (not token) {
        return std::unexpected{token.error()};
      }
      if (std::ranges::find(seen, *token) != seen.end()) {
        return std::unexpected{fault(ParseErrc::bad_member, element)};
      }
      seen.push_back(*token);
      if (const std::optional<E> value = lookup(*token)) {
        set.insert(*value);
      } else {
        unknowns_.add(std::string{path} + "[]=" + detail::subject_of(*token),
                      element.offset);
      }
    }
    return set;
  }

  // ---- the provider blocks ----

  [[nodiscard]] Decoded<core::Capabilities<Coops>> coops_of(
      const Value& block) {
    if (auto o = object_of(block); not o) {
      return std::unexpected{std::move(o).error()};
    }
    note_unknown(block, coops_keep, "stations[].coops");
    core::Capabilities<Coops> caps;
    if (const Value* v = json::member(block, "observed")) {
      auto set =
          set_of(*v, tokens::observed_tokens, "stations[].coops.observed");
      if (not set) {
        return std::unexpected{std::move(set).error()};
      }
      caps.observed = *set;
    }
    if (const Value* v = json::member(block, "met")) {
      auto set = set_of(*v, tokens::met_tokens, "stations[].coops.met");
      if (not set) {
        return std::unexpected{std::move(set).error()};
      }
      caps.met = *set;
    }
    if (const Value* v = json::member(block, "air_gap")) {
      auto b = bool_of(*v);
      if (not b) {
        return std::unexpected{std::move(b).error()};
      }
      caps.air_gap = *b;
    }
    if (const Value* v = json::member(block, "predictions")) {
      auto token = string_of(*v);
      if (not token) {
        return std::unexpected{std::move(token).error()};
      }
      if (const auto p = tokens::value_of(tokens::predictions_tokens, *token)) {
        caps.predictions = *p;
      } else {
        unknowns_.add(
            "stations[].coops.predictions=" + detail::subject_of(*token),
            v->offset);
      }
    }
    if (const Value* v = json::member(block, "datums_offered")) {
      auto set =
          set_by(*v, tokens::coops_datum_of, "stations[].coops.datums_offered");
      if (not set) {
        return std::unexpected{std::move(set).error()};
      }
      caps.datums_offered = *set;
    }
    if (const Value* v = json::member(block, "datums")) {
      auto table = datums_of(*v);
      if (not table) {
        return std::unexpected{std::move(table).error()};
      }
      caps.datums = *std::move(table);
    }
    return caps;
  }

  // Heights in metres above MSL, by datum token; MSL itself, if listed, is 0.
  [[nodiscard]] Decoded<core::DatumTable> datums_of(const Value& object) {
    const auto members = object_of(object);
    if (not members) {
      return std::unexpected{members.error()};
    }
    note_unknown(object, datums_keep, "stations[].coops.datums");
    std::vector<core::DatumHeight> rows;
    for (const json::Member& m : **members) {
      const auto datum = tokens::vertical_datum_of(m.name);
      if (not datum) {
        continue;  // unknown: noted above
      }
      const auto metres = number_of(m.value);
      if (not metres) {
        return std::unexpected{metres.error()};
      }
      const core::Length height =
          tokens::canonical(core::Length::in(*metres, core::LengthUnit::meter));
      if (*datum == core::VerticalDatum::msl and
          height.as(core::LengthUnit::meter) != 0.0) {
        return std::unexpected{fault(ParseErrc::out_of_range, m.value)};
      }
      rows.push_back({.datum = *datum, .height = height});
    }
    auto table = core::DatumTable::from_heights(core::VerticalDatum::msl, rows);
    if (not table) {  // unreachable: unique, finite, MSL 0
      return std::unexpected{fault(ParseErrc::out_of_range, object)};
    }
    return *table;
  }

  [[nodiscard]] Decoded<core::usgs::SeriesInfo> series_of(const Value& row) {
    if (auto o = object_of(row); not o) {
      return std::unexpected{std::move(o).error()};
    }
    note_unknown(row, series_keep, "stations[].usgs.series[]");
    auto id = code_of<core::usgs::TimeSeriesId>(row, "id");
    if (not id) {
      return std::unexpected{std::move(id).error()};
    }
    auto parameter = code_of<core::usgs::ParameterCode>(row, "parameter");
    if (not parameter) {
      return std::unexpected{std::move(parameter).error()};
    }
    auto statistic = code_of<core::usgs::StatisticId>(row, "statistic");
    if (not statistic) {
      return std::unexpected{std::move(statistic).error()};
    }
    std::string name;
    if (const Value* v = json::member(row, "parameter_name")) {
      auto s = string_of(*v);
      if (not s) {
        return std::unexpected{std::move(s).error()};
      }
      name = std::string{*s};
    }
    bool primary = false;
    if (const Value* v = json::member(row, "primary")) {
      auto b = bool_of(*v);
      if (not b) {
        return std::unexpected{std::move(b).error()};
      }
      primary = *b;
    }
    std::optional<core::Unit> unit;
    if (const Value* v = json::member(row, "unit")) {
      auto s = string_of(*v);
      if (not s) {
        return std::unexpected{std::move(s).error()};
      }
      unit = core::parse_unit(*s);
      if (not unit) {  // blank text: write no member instead
        return std::unexpected{fault(ParseErrc::bad_member, *v)};
      }
    }
    auto validity = validity_of(row);
    if (not validity) {
      return std::unexpected{std::move(validity).error()};
    }
    return core::usgs::SeriesInfo{.id = *id,
                                  .parameter = *parameter,
                                  .statistic = *statistic,
                                  .parameter_name = std::move(name),
                                  .primary = primary,
                                  .unit = std::move(unit),
                                  .validity = *validity};
  }

  // The series, and how many there are (against the element limit).
  [[nodiscard]] Decoded<core::Capabilities<Usgs>> usgs_of(
      const Value& block, std::string_view station_id) {
    if (auto o = object_of(block); not o) {
      return std::unexpected{std::move(o).error()};
    }
    note_unknown(block, usgs_keep, "stations[].usgs");
    const Value* list = json::member(block, "series");
    if (list == nullptr) {
      return core::Capabilities<Usgs>{};
    }
    const auto rows = array_of(*list);
    if (not rows) {
      return std::unexpected{rows.error()};
    }
    std::vector<core::usgs::SeriesInfo> series;
    series.reserve((*rows)->size());
    for (const Value& row : **rows) {
      auto info = series_of(row);
      if (not info) {
        return std::unexpected{std::move(info).error()};
      }
      series.push_back(*std::move(info));
    }
    auto set = core::usgs::SeriesSet::make(std::move(series));
    if (not set) {
      return std::unexpected{Fault{.offset = list->offset,
                                   .error = Error{detail::format_error(
                                       FormatErrc::inconsistent_metadata,
                                       std::string{station_id} + ": " +
                                           series_error_text(set.error()))}}};
    }
    return core::Capabilities<Usgs>{.series = *std::move(set)};
  }

  [[nodiscard]] Decoded<core::Capabilities<Ndbc>> ndbc_of(const Value& block) {
    if (auto o = object_of(block); not o) {
      return std::unexpected{std::move(o).error()};
    }
    note_unknown(block, ndbc_keep, "stations[].ndbc");
    auto owner = optional_text(block, "owner");
    if (not owner) {
      return std::unexpected{std::move(owner).error()};
    }
    auto program = optional_text(block, "program");
    if (not program) {
      return std::unexpected{std::move(program).error()};
    }
    auto platform = optional_text(block, "platform_type");
    if (not platform) {
      return std::unexpected{std::move(platform).error()};
    }
    std::optional<core::Length> elevation;
    if (const Value* v = json::member(block, "elevation_m")) {
      auto metres = number_of(*v);
      if (not metres) {
        return std::unexpected{std::move(metres).error()};
      }
      elevation =
          tokens::canonical(core::Length::in(*metres, core::LengthUnit::meter));
    }
    return core::Capabilities<Ndbc>{.owner = *std::move(owner),
                                    .program = *std::move(program),
                                    .platform_type = *std::move(platform),
                                    .elevation = elevation};
  }

  // ---- a station ----

  [[nodiscard]] Decoded<AnyEntry> station_of(const Value& station) {
    if (auto o = object_of(station); not o) {
      return std::unexpected{std::move(o).error()};
    }
    note_unknown(station, station_keep, "stations[]");
    // Exactly one provider block: it says which provider's station this is.
    constexpr std::array<std::string_view, 3> blocks{"coops", "usgs", "ndbc"};
    const Value* block = nullptr;
    std::size_t which = 0;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
      const Value* v = json::member(station, blocks.at(i));
      if (v == nullptr) {
        continue;
      }
      if (block != nullptr) {
        return std::unexpected{
            fault(ParseErrc::bad_member, std::max(v->offset, block->offset))};
      }
      block = v;
      which = i;
    }
    if (block == nullptr) {
      return std::unexpected{fault(ParseErrc::bad_member, station)};
    }
    switch (which) {
      case 0:
        return entry_of<Coops>(
            station, *block,
            [&](const Value& b, std::string_view) { return coops_of(b); });
      case 1:
        return entry_of<Usgs>(station, *block,
                              [&](const Value& b, std::string_view id) {
                                return usgs_of(b, id);
                              });
      default:
        break;
    }
    return entry_of<Ndbc>(
        station, *block,
        [&](const Value& b, std::string_view) { return ndbc_of(b); });
  }

 private:
  // A USGS code member (Code::make, exactly as written).
  template <class Code>
  [[nodiscard]] Decoded<Code> code_of(const Value& row,
                                      std::string_view name) const {
    return required(row, name).and_then([&](const Value* v) {
      return string_of(*v).and_then([&](std::string_view s) -> Decoded<Code> {
        auto c = Code::make(s);
        if (not c) {
          return std::unexpected{fault(ParseErrc::bad_member, *v)};
        }
        return *c;
      });
    });
  }

  template <class P, class Caps>
  [[nodiscard]] Decoded<AnyEntry> entry_of(const Value& station,
                                           const Value& block, Caps caps_of) {
    const auto id_value = required(station, "id");
    if (not id_value) {
      return std::unexpected{id_value.error()};
    }
    const auto raw = string_of(**id_value);
    if (not raw) {
      return std::unexpected{raw.error()};
    }
    // The id exactly as the provider's canonical form spells it.
    const auto id = core::StationId<P>::make(*raw);
    if (not id or id->value() != *raw) {
      return std::unexpected{fault(ParseErrc::bad_member, **id_value)};
    }
    auto name = required(station, "name").and_then([&](const Value* v) {
      return text_of(*v);
    });
    if (not name) {
      return std::unexpected{std::move(name).error()};
    }
    auto location = location_of(station);
    if (not location) {
      return std::unexpected{std::move(location).error()};
    }
    auto validity = validity_of(station);
    if (not validity) {
      return std::unexpected{std::move(validity).error()};
    }
    auto caps = caps_of(block, id->value());
    if (not caps) {
      return std::unexpected{std::move(caps).error()};
    }
    return AnyEntry{CatalogEntry<P>{.station = {.id = *id,
                                                .name = *std::move(name),
                                                .location = *location,
                                                .validity = *validity},
                                    .caps = *std::move(caps)}};
  }

  [[nodiscard]] Decoded<core::Location> location_of(
      const Value& station) const {
    const auto lon_value = required(station, "lon");
    if (not lon_value) {
      return std::unexpected{lon_value.error()};
    }
    const auto lat_value = required(station, "lat");
    if (not lat_value) {
      return std::unexpected{lat_value.error()};
    }
    const auto lon = number_of(**lon_value);
    if (not lon) {
      return std::unexpected{lon.error()};
    }
    const auto lat = number_of(**lat_value);
    if (not lat) {
      return std::unexpected{lat.error()};
    }
    const auto location = core::Location::make({.lat = *lat, .lon = *lon});
    if (not location) {
      const Value& at =
          location.error() == core::LocationError::latitude_out_of_range
              ? **lat_value
              : **lon_value;
      return std::unexpected{fault(ParseErrc::out_of_range, at)};
    }
    return tokens::canonical(*location);
  }

  [[nodiscard]] static std::string series_error_text(
      const core::usgs::SeriesSetError& e) {
    return std::visit(
        core::Overloaded{[](const core::usgs::DuplicateSeriesId& d) {
                           return "series " + std::string{d.id.view()} +
                                  " repeated";
                         },
                         [](const core::usgs::SeveralPrimary& p) {
                           return "several primary series of " +
                                  std::string{p.parameter.view()} + "/" +
                                  std::string{p.statistic.view()};
                         }},
        e);
  }

  std::string_view body_;
  Unknowns unknowns_;
};

// ---- the document
// ------------------------------------------------------------

struct Version {
  unsigned major;
  unsigned minor;
};

// "<major>.<minor>": digits without a leading zero (but "0"), at most 9 each.
std::optional<Version> parse_version(std::string_view text) {
  const auto number = [](std::string_view digits) -> std::optional<unsigned> {
    if (digits.empty() or digits.size() > 9 or
        (digits.size() > 1 and digits.front() == '0') or
        not std::ranges::all_of(digits, core::ascii::is_digit)) {
      return std::nullopt;
    }
    unsigned value = 0;
    for (const char c : digits) {
      value = (value * 10U) + static_cast<unsigned>(c - '0');
    }
    return value;
  };
  const std::size_t dot = text.find('.');
  if (dot == std::string_view::npos) {
    return std::nullopt;
  }
  const auto major = number(text.substr(0, dot));
  const auto minor = number(text.substr(dot + 1));
  if (not major or not minor) {
    return std::nullopt;
  }
  return Version{.major = *major, .minor = *minor};
}

// What a station costs against ReadLimits::max_elements: its share, and
// each of its USGS series.
std::size_t cost_of(const Value& station) {
  std::size_t cost = elements_per_station;
  if (const Value* usgs = json::member(station, "usgs")) {
    if (const Value* series = json::member(*usgs, "series")) {
      if (const json::Array* rows = json::as_array(*series)) {
        cost += rows->size();
      }
    }
  }
  return cost;
}

template <class P>
std::expected<StationCatalog, Error> make_catalog(
    CatalogHeader header, std::vector<CatalogEntry<P>> entries) {
  auto catalog = Catalog<P>::make(std::move(header), std::move(entries));
  if (not catalog) {
    return detail::fail(
        detail::format_error(FormatErrc::duplicate_station_id,
                             std::string{catalog.error().id.value()}));
  }
  return StationCatalog{*std::move(catalog)};
}

// The provider's index among the three blocks (AnyEntry's order).
std::size_t index_of(core::DataSource s) noexcept {
  switch (s) {
    case core::DataSource::noaa_coops:
      return 0;
    case core::DataSource::usgs:
      return 1;
    default:
      break;
  }
  return 2;
}

// The stations decoded while the body streams by, by provider, with where
// the first of each provider is, and the first failure (kept for after the
// format and version are known).
class Stations {
 public:
  // Decodes one element of the stations array; an error only past the
  // element limit.
  std::expected<void, Error> add(Decoder& decoder, std::string_view text,
                                 const Value& element, const ReadContext& ctx) {
    elements_ += cost_of(element);
    if (elements_ > ctx.limits.max_elements) {
      return detail::fail(ParseError::make(
          ParseErrc::too_large, detail::where_of(text, element.offset),
          std::to_string(elements_) + " elements, limit " +
              std::to_string(ctx.limits.max_elements)));
    }
    if (fault_) {
      return {};
    }
    auto entry = decoder.station_of(element);
    if (not entry) {
      fault_ = std::move(entry).error();
      return {};
    }
    auto& first = first_offset_.at(entry->index());
    first = first.value_or(element.offset);
    std::visit(
        core::Overloaded{
            [&](CatalogEntry<Coops>& e) { coops_.push_back(std::move(e)); },
            [&](CatalogEntry<Usgs>& e) { usgs_.push_back(std::move(e)); },
            [&](CatalogEntry<Ndbc>& e) { ndbc_.push_back(std::move(e)); }},
        *entry);
    return {};
  }

  [[nodiscard]] const std::optional<Fault>& fault() const noexcept {
    return fault_;
  }

  // Where the first station of a provider other than `provider` is.
  [[nodiscard]] std::optional<std::size_t> first_of_another(
      core::DataSource provider) const {
    std::optional<std::size_t> first;
    for (std::size_t i = 0; i < first_offset_.size(); ++i) {
      const std::optional<std::size_t>& at = first_offset_.at(i);
      if (i != index_of(provider) and at) {
        first = std::min(first.value_or(*at), *at);
      }
    }
    return first;
  }

  // The catalog of the provider's stations.
  [[nodiscard]] std::expected<StationCatalog, Error> catalog(
      core::DataSource provider, CatalogHeader header) && {
    switch (index_of(provider)) {
      case 0:
        return make_catalog(std::move(header), std::move(coops_));
      case 1:
        return make_catalog(std::move(header), std::move(usgs_));
      default:
        break;
    }
    return make_catalog(std::move(header), std::move(ndbc_));
  }

 private:
  std::vector<CatalogEntry<Coops>> coops_;
  std::vector<CatalogEntry<Usgs>> usgs_;
  std::vector<CatalogEntry<Ndbc>> ndbc_;
  std::array<std::optional<std::size_t>, 3> first_offset_{};
  std::optional<Fault> fault_;
  std::size_t elements_ = 0;
};

// format and version: what the file is, before anything else is judged.
std::expected<Version, Error> identify(const Value& root) {
  const Value* format = json::member(root, "format");
  const std::string* format_text =
      format == nullptr ? nullptr : json::as_string(*format);
  if (format_text == nullptr or *format_text != catalog_format) {
    return detail::fail(detail::format_error(
        FormatErrc::not_this_format, format_text == nullptr
                                         ? std::string{}
                                         : detail::subject_of(*format_text)));
  }
  const Value* version = json::member(root, "version");
  const std::string* version_text =
      version == nullptr ? nullptr : json::as_string(*version);
  if (version_text == nullptr) {
    return detail::fail(detail::format_error(FormatErrc::bad_version, ""));
  }
  const auto parsed = parse_version(*version_text);
  if (not parsed) {
    return detail::fail(detail::format_error(
        FormatErrc::bad_version, detail::subject_of(*version_text)));
  }
  if (parsed->major != catalog_major) {
    return detail::fail(detail::format_error(
        FormatErrc::unsupported_version, detail::subject_of(*version_text)));
  }
  return *parsed;
}

// The provider the file names: one of the three catalog providers.
Decoded<core::DataSource> provider_of(Decoder& d, const Value& root) {
  return d.required(root, "provider").and_then([&](const Value* v) {
    return d.string_of(*v).and_then(
        [&](std::string_view s) -> Decoded<core::DataSource> {
          const auto source = core::parse_data_source(s);
          if (not source or (*source != core::DataSource::noaa_coops and
                             *source != core::DataSource::usgs and
                             *source != core::DataSource::ndbc)) {
            return std::unexpected{d.fault(ParseErrc::bad_member, *v)};
          }
          return *source;
        });
  });
}

Decoded<CatalogHeader> header_of(Decoder& d, const Value& root) {
  auto generated = d.required(root, "generated").and_then([&](const Value* v) {
    return d.timestamp_of(*v);
  });
  if (not generated) {
    return std::unexpected{std::move(generated).error()};
  }
  const auto list = d.required(root, "sources").and_then([&](const Value* v) {
    return d.array_of(*v);
  });
  if (not list) {
    return std::unexpected{list.error()};
  }
  std::vector<CatalogSource> sources;
  for (const Value& source : **list) {
    if (auto o = d.object_of(source); not o) {
      return std::unexpected{std::move(o).error()};
    }
    d.note_unknown(source, source_keep, "sources[]");
    auto url = d.required(source, "url").and_then([&](const Value* v) {
      return d.text_of(*v);
    });
    if (not url) {
      return std::unexpected{std::move(url).error()};
    }
    auto retrieved =
        d.required(source, "retrieved").and_then([&](const Value* v) {
          return d.timestamp_of(*v);
        });
    if (not retrieved) {
      return std::unexpected{std::move(retrieved).error()};
    }
    sources.push_back({.url = *std::move(url), .retrieved = *retrieved});
  }
  return CatalogHeader{.generated = *generated, .sources = std::move(sources)};
}

// The members of the header, and the first value the format refuses in
// document order: the header's, the first station failure, a station of
// another provider, and (unless the file is of a newer minor) the first
// unknown member or token.
struct Header {
  core::DataSource provider;
  CatalogHeader header;
};

Decoded<Header> judge(Decoder& d, const Value& root, const Stations& stations,
                      bool newer) {
  std::vector<Fault> faults;
  d.note_unknown(root, document_keep, "");
  const auto provider = provider_of(d, root);
  if (not provider) {
    faults.push_back(provider.error());
  } else if (const auto other = stations.first_of_another(*provider)) {
    faults.push_back(d.fault(ParseErrc::bad_member, *other));
  }
  auto header = header_of(d, root);
  if (not header) {
    faults.push_back(header.error());
  }
  const Value* list = json::member(root, "stations");
  if (list == nullptr or json::as_array(*list) == nullptr) {
    faults.push_back(
        d.fault(ParseErrc::bad_member, list == nullptr ? root : *list));
  }
  if (stations.fault()) {
    faults.push_back(*stations.fault());
  }
  if (const auto unknown = d.unknowns().first(); unknown and not newer) {
    faults.push_back(d.fault(ParseErrc::bad_member, *unknown));
  }
  if (not faults.empty()) {
    return std::unexpected{
        std::ranges::min(faults, std::less<>{}, &Fault::offset)};
  }
  return Header{.provider = *provider, .header = *std::move(header)};
}

// `generated` no more than catalog_clock_tolerance after `now`.
std::expected<void, Error> check_clock(const StationCatalog& catalog,
                                       core::Time now) {
  const core::Time generated =
      std::visit([](const auto& c) { return c.header().generated; }, catalog);
  if (generated - now > catalog_clock_tolerance) {
    std::string when;
    detail::json_text::append_timestamp(when, generated);
    return detail::fail(detail::format_error(FormatErrc::future_dated,
                                             when.substr(1, when.size() - 2)));
  }
  return {};
}

}  // namespace

std::expected<Read<StationCatalog>, Error> parse_station_catalog(
    std::string_view text, core::Time now, const ReadContext& ctx) {
  Decoder decoder{text};
  Stations stations;
  const json::OnElement on_station =
      [&](std::size_t /*index*/,
          const Value& element) -> std::expected<void, Error> {
    return stations.add(decoder, text, element, ctx);
  };
  const auto document = json::parse(text, document_keep, ctx, on_station);
  if (not document) {
    return std::unexpected{document.error()};
  }
  const auto version = identify(document->root);
  if (not version) {
    return std::unexpected{version.error()};
  }
  const bool newer = version->minor > catalog_minor;
  auto header = judge(decoder, document->root, stations, newer);
  if (not header) {
    return std::unexpected{std::move(header).error().error};
  }
  auto catalog =
      std::move(stations).catalog(header->provider, std::move(header->header));
  if (not catalog) {
    return std::unexpected{std::move(catalog).error()};
  }
  if (auto clock = check_clock(*catalog, now); not clock) {
    return std::unexpected{std::move(clock).error()};
  }
  std::vector<Warning> warnings;
  if (newer) {
    warnings.push_back({.code = WarningCode::minor_newer,
                        .subject = std::to_string(version->major) + "." +
                                   std::to_string(version->minor)});
    decoder.unknowns().report(warnings);
  }
  return Read<StationCatalog>{.value = *std::move(catalog),
                              .warnings = std::move(warnings)};
}

std::expected<Read<StationCatalog>, Error> read_station_catalog(
    const std::filesystem::path& path, core::Time now, const ReadContext& ctx) {
  auto bytes = read_text_file(path, ctx.limits);
  if (not bytes) {
    return detail::fail(std::move(bytes).error());
  }
  return parse_station_catalog(*bytes, now, ctx);
}

}  // namespace mov::io
