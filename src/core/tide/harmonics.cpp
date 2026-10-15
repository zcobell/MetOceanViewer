// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/tide/harmonics.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/detail/numeric.hpp"
#include "mov/core/overloaded.hpp"
#include "mov/core/station.hpp"
#include "mov/core/tide/catalogue.hpp"
#include "mov/core/tide/constituent.hpp"
#include "mov/core/tide/constituent_set.hpp"
#include "mov/core/tide/datums.hpp"
#include "mov/core/tide/harmonics_input.hpp"
#include "mov/core/tide/licence.hpp"
#include "mov/core/tide/source.hpp"
#include "mov/core/tide/station_info.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"

namespace mov::core::detail {

struct ReferenceRecord {
  tide::StationInfo info;
  std::vector<tide::Constant> constants;
  Length z0;
  std::optional<tide::Analysis> analysis;
  friend bool operator==(const ReferenceRecord&,
                         const ReferenceRecord&) = default;
};

struct SubordinateRecord {
  tide::StationInfo info;
  tide::ReferenceIndex reference;
  tide::SubordinateOffsets offsets;
  Length reference_datum_height;  // derived from the reference's datums
  friend bool operator==(const SubordinateRecord&,
                         const SubordinateRecord&) = default;
};

struct HarmonicsData {
  tide::FileHeader header;
  std::vector<tide::Source> sources;  // sorted by id
  tide::ConstituentSet constituents;
  std::vector<ReferenceRecord> references;      // sorted by id
  std::vector<SubordinateRecord> subordinates;  // sorted by id
  friend bool operator==(const HarmonicsData&, const HarmonicsData&) = default;
};

/// The one friend of the file's index and view types: makes them from
/// positions it has checked.
struct HarmonicsAccess {
  static tide::SourceIndex source(std::size_t i) noexcept {
    return tide::SourceIndex{i};
  }
  static tide::ReferenceIndex reference(std::size_t i) noexcept {
    return tide::ReferenceIndex{i};
  }
  static tide::SubordinateIndex subordinate(std::size_t i) noexcept {
    return tide::SubordinateIndex{i};
  }
  static tide::ReferenceView reference_view(const HarmonicsData& d,
                                            tide::ReferenceIndex i) noexcept {
    return tide::ReferenceView{d, i};
  }
  static tide::SubordinateView subordinate_view(
      const HarmonicsData& d, tide::SubordinateIndex i) noexcept {
    return tide::SubordinateView{d, i};
  }
  static tide::HarmonicsFile file(std::shared_ptr<const HarmonicsData> d) {
    return tide::HarmonicsFile{std::move(d)};
  }
};

}  // namespace mov::core::detail

namespace mov::core::tide {

namespace {

using detail::HarmonicsAccess;
using detail::HarmonicsData;
using detail::ReferenceRecord;
using detail::SubordinateRecord;

/// docs/harmonics-json.md 7.1: |speed - derived| <= 1e-5 deg/h.
constexpr double max_speed_difference = 1e-5;

const StationInput& common(const StationKindInput& s) {
  return std::visit(
      [](const auto& station) -> const StationInput& {
        return station.station;
      },
      s);
}

std::optional<std::chrono::sys_days> calendar_date(
    const std::chrono::year_month_day& d) noexcept {
  return d.ok() ? std::optional{std::chrono::sys_days{d}} : std::nullopt;
}

std::optional<Time> timestamp(const TimestampInput& t) noexcept {
  const auto day = calendar_date(t.date);
  if (not day or t.hour < 0 or t.hour > 23 or t.minute < 0 or t.minute > 59 or
      t.second < 0 or t.second > 59) {
    return std::nullopt;
  }
  return Time{*day} + std::chrono::hours{t.hour} +
         std::chrono::minutes{t.minute} + std::chrono::seconds{t.second};
}

/// What the checks found out on the way, for the build: each value is set
/// where its check passed.
struct Resolution {
  std::optional<Time> created;
  std::vector<std::optional<Licence>> source_licences;
  std::vector<std::optional<std::chrono::sys_days>> retrieved;
  std::map<std::string_view, std::size_t> source_by_id;  // first of each id
  std::vector<DictionaryEntry> entries;                  // input order
  std::vector<std::optional<Argument>> arguments;
  std::vector<std::optional<std::size_t>> station_source;
  std::vector<std::optional<Licence>> station_licences;
  std::vector<std::optional<Record>> records;
  std::vector<std::optional<DatumPeriod>> periods;
  std::vector<std::optional<std::size_t>> subordinate_reference;
};

/// A reference's computed_datums, if it is a reference that has them.
const ComputedDatumsInput* computed_datums_of(
    const ReferenceInput* reference) noexcept {
  if (reference == nullptr or not reference->computed_datums) {
    return nullptr;
  }
  return &*reference->computed_datums;
}

/// A Resolution sized for `in`, with the dictionary's entries copied (the
/// checks look names up in them; the build moves them into the set).
Resolution initial(const HarmonicsInput& in) {
  Resolution r;
  r.source_licences.resize(in.sources.size());
  r.retrieved.resize(in.sources.size());
  r.arguments.resize(in.constituents.size());
  r.station_source.resize(in.stations.size());
  r.station_licences.resize(in.stations.size());
  r.records.resize(in.stations.size());
  r.periods.resize(in.stations.size());
  r.subordinate_reference.resize(in.stations.size());
  r.entries.reserve(in.constituents.size());
  for (const ConstituentInput& c : in.constituents) {
    r.entries.push_back(c.entry);
  }
  return r;
}

/// The checks of HarmonicsFile::make in their pinned order. Fail-first stops
/// at the first error; otherwise every check runs that does not depend on a
/// failed one.
class Checker {
 public:
  Checker(const HarmonicsInput& input, bool fail_first)
      : in_{input}, fail_first_{fail_first}, r_{initial(input)} {}
  Checker(const Checker&) = delete;
  Checker& operator=(const Checker&) = delete;
  Checker(Checker&&) = delete;
  Checker& operator=(Checker&&) = delete;
  ~Checker() = default;

  void run() {
    const std::array steps{&Checker::check_created, &Checker::check_sources,
                           &Checker::check_dictionary, &Checker::check_stations,
                           &Checker::check_cross};
    for (const auto step : steps) {
      if (stopped()) {
        return;
      }
      (this->*step)();
    }
  }

  [[nodiscard]] std::vector<HarmonicsError>& errors() noexcept {
    return errors_;
  }
  [[nodiscard]] Resolution& resolution() noexcept { return r_; }
  [[nodiscard]] const detail::EntryLookup& lookup() const noexcept {
    return lookup_;
  }

 private:
  /// Records `e`; false when the caller must stop.
  bool report(HarmonicsError e) {
    errors_.push_back(e);
    return not stopped();
  }
  [[nodiscard]] bool stopped() const noexcept {
    return fail_first_ and not errors_.empty();
  }

  void check_created() {
    r_.created = timestamp(in_.header.created);
    if (not r_.created) {
      report(InvalidCreated{});
    }
  }

  void check_sources() {
    for (std::size_t i = 0; i < in_.sources.size(); ++i) {
      const SourceInput& s = in_.sources[i];
      const auto [first, inserted] =
          r_.source_by_id.try_emplace(s.id.view(), i);
      if (not inserted and
          not report(DuplicateSource{.source = i, .first = first->second})) {
        return;
      }
      auto licence = Licence::parse(s.licence);
      if (licence) {
        r_.source_licences[i] = *std::move(licence);
      } else if (not report(InvalidSourceLicence{.source = i,
                                                 .error = licence.error()})) {
        return;
      }
      r_.retrieved[i] = calendar_date(s.retrieved);
      if (not r_.retrieved[i] and not report(InvalidRetrieved{.source = i})) {
        return;
      }
    }
  }

  void check_dictionary() {
    const auto first = detail::earlier_same_name(r_.entries);
    for (std::size_t i = 0; i < r_.entries.size(); ++i) {
      if (const std::optional<std::size_t>& earlier = first[i];
          earlier and
          not report(DuplicateName{.entry = i, .first = *earlier})) {
        return;
      }
      const auto argument = flatten(r_.entries[i].definition, lookup_);
      if (not argument) {
        if (not report(InvalidTerm{.entry = i, .error = argument.error()})) {
          return;
        }
        continue;
      }
      r_.arguments[i] = *argument;
      const double derived = argument->speed_deg_per_hour();
      if (not(std::abs(in_.constituents[i].speed - derived) <=
              max_speed_difference) and
          not report(SpeedMismatch{.entry = i, .derived = derived})) {
        return;
      }
    }
  }

  void check_stations() {
    for (std::size_t s = 0; s < in_.stations.size(); ++s) {
      if (not check_station(s)) {
        return;
      }
    }
  }

  /// One station's own rules, in the pinned order; false when the caller
  /// must stop.
  bool check_station(std::size_t s) {
    const StationInput& info = common(in_.stations[s]);
    const auto* reference = std::get_if<ReferenceInput>(&in_.stations[s]);
    const ComputedDatumsInput* computed = computed_datums_of(reference);
    return check_namespace(s, info) and check_station_licence(s, info) and
           (reference == nullptr or check_rows(s, *reference)) and
           (reference == nullptr or check_z0(s, *reference)) and
           check_record_years(s, info) and
           (computed == nullptr or
            check_computed_tokens(s, info.datums, computed->heights)) and
           (not info.record or check_record(s, *info.record)) and
           (computed == nullptr or check_period(s, *computed));
  }

  bool check_namespace(std::size_t s, const StationInput& info) {
    const auto ns = station_namespace(info.id.value());
    const auto source =
        ns ? r_.source_by_id.find(ns->view()) : r_.source_by_id.end();
    if (source == r_.source_by_id.end()) {
      return report(UnknownSource{.station = s});
    }
    r_.station_source[s] = source->second;
    return true;
  }

  bool check_station_licence(std::size_t s, const StationInput& info) {
    if (not info.licence) {
      return true;
    }
    auto licence = Licence::parse(*info.licence);
    if (not licence) {
      return report(
          InvalidStationLicence{.station = s, .error = licence.error()});
    }
    r_.station_licences[s] = *std::move(licence);
    return true;
  }

  bool check_z0(std::size_t s, const ReferenceInput& reference) {
    return detail::is_finite(reference.z0.as(LengthUnit::meter)) or
           report(InvalidNumber{.station = s, .number = StationNumber::z0});
  }

  bool check_record_years(std::size_t s, const StationInput& info) {
    const auto years = info.record ? info.record->years : std::nullopt;
    return not years or (detail::is_finite(*years) and *years > 0.0) or
           report(InvalidNumber{.station = s,
                                .number = StationNumber::record_years});
  }

  bool check_rows(std::size_t s, const ReferenceInput& reference) {
    std::map<std::string_view, std::size_t> seen;
    for (std::size_t row = 0; row < reference.constants.size(); ++row) {
      const std::string_view name = reference.constants[row].constituent.view();
      if (not lookup_.index(name)) {
        if (not report(UnknownConstituent{.station = s, .row = row})) {
          return false;
        }
        continue;
      }
      const auto [first, inserted] = seen.try_emplace(name, row);
      if (not inserted and
          not report(
              DuplicateRow{.station = s, .row = row, .first = first->second})) {
        return false;
      }
    }
    return true;
  }

  bool check_computed_tokens(std::size_t s, const DatumHeights& published,
                             const DatumHeights& computed) {
    // all_of stops where report() says stop.
    const auto computable = [&](DatumToken d) {
      return not computed.height(d) or is_computable(d) or
             report(UncomputableDatum{.station = s, .datum = d});
    };
    const auto one_origin = [&](DatumToken d) {
      return not published.height(d) or not computed.height(d) or
             report(DatumConflict{.station = s, .datum = d});
    };
    return std::ranges::all_of(all_datum_tokens, computable) and
           std::ranges::all_of(all_datum_tokens, one_origin);
  }

  /// Reports `d` if it is present and not a calendar date, clearing
  /// `all_valid`; false when the caller must stop.
  bool date_ok(std::size_t s,
               const std::optional<std::chrono::year_month_day>& d,
               StationDate which, bool& all_valid) {
    if (not d or d->ok()) {
      return true;
    }
    all_valid = false;
    return report(InvalidStationDate{.station = s, .date = which});
  }

  bool check_record(std::size_t s, const RecordInput& record) {
    bool valid = true;
    if (not date_ok(s, record.start, StationDate::record_start, valid) or
        not date_ok(s, record.end, StationDate::record_end, valid)) {
      return false;
    }
    if (not valid) {
      return true;
    }
    const auto day = [](const std::optional<std::chrono::year_month_day>& d) {
      return d ? calendar_date(*d) : std::nullopt;
    };
    const auto dates =
        ValidRange::make({.first = day(record.start), .last = day(record.end)});
    if (not dates) {
      return report(InvertedDates{.station = s, .range = DateRange::record});
    }
    r_.records[s] = Record{.dates = *dates, .years = record.years};
    return true;
  }

  bool check_period(std::size_t s, const ComputedDatumsInput& computed) {
    bool valid = true;
    if (not date_ok(s, computed.start, StationDate::period_start, valid) or
        not date_ok(s, computed.end, StationDate::period_end, valid)) {
      return false;
    }
    if (not valid) {
      return true;
    }
    const std::chrono::sys_days start{computed.start};
    const std::chrono::sys_days end{computed.end};
    if (end < start) {
      return report(InvertedDates{.station = s, .range = DateRange::period});
    }
    if (not spans_nodal_cycle(start, end)) {
      return report(ShortDatumPeriod{.station = s});
    }
    r_.periods[s] = DatumPeriod{.start = start, .end = end};
    return true;
  }

  void check_cross() {
    std::map<std::string_view, std::size_t> by_id;
    for (std::size_t s = 0; s < in_.stations.size(); ++s) {
      const auto [first, inserted] =
          by_id.try_emplace(common(in_.stations[s]).id.value(), s);
      if (not inserted and
          not report(DuplicateStation{.station = s, .first = first->second})) {
        return;
      }
    }
    for (std::size_t s = 0; s < in_.stations.size(); ++s) {
      const auto* sub = std::get_if<SubordinateInput>(&in_.stations[s]);
      if (sub and not check_subordinate(s, *sub, by_id)) {
        return;
      }
    }
  }

  bool check_subordinate(std::size_t s, const SubordinateInput& sub,
                         const std::map<std::string_view, std::size_t>& by_id) {
    const auto found = by_id.find(sub.reference.value());
    if (found == by_id.end()) {
      return report(ReferenceMissing{.station = s});
    }
    const auto* reference =
        std::get_if<ReferenceInput>(&in_.stations[found->second]);
    if (not reference) {
      return report(
          ReferenceNotReference{.station = s, .referenced = found->second});
    }
    r_.subordinate_reference[s] = found->second;
    const auto* token = std::get_if<DatumToken>(&sub.offsets.datum());
    if (token == nullptr or reference->station.datums.height(*token) or
        (reference->computed_datums and
         reference->computed_datums->heights.height(*token))) {
      return true;
    }
    return report(OffsetDatumMissing{.station = s, .datum = *token});
  }

  const HarmonicsInput& in_;
  bool fail_first_;
  std::vector<HarmonicsError> errors_;
  Resolution r_;
  detail::EntryLookup lookup_{r_.entries};  // after r_: views its entries
};

// ---------------------------------------------------------------------------
// Warnings.

void redefined_constituents(const Resolution& r,
                            std::vector<HarmonicsWarning>& out) {
  for (std::size_t i = 0; i < r.entries.size(); ++i) {
    const auto canonical = canonical_name(r.entries[i].name.view());
    if (not canonical or not r.arguments[i]) {
      continue;
    }
    if (catalogue_argument(*canonical) != r.arguments[i]) {
      out.emplace_back(ConstituentRedefined{.entry = i});
    }
  }
}

void unsupported_datums(const HarmonicsInput& in,
                        std::vector<HarmonicsWarning>& out) {
  for (const DatumToken d : all_datum_tokens) {
    if (to_vertical_datum(d)) {
      continue;
    }
    std::optional<DatumUnsupported> found;
    for (std::size_t s = 0; s < in.stations.size(); ++s) {
      const auto* reference = std::get_if<ReferenceInput>(&in.stations[s]);
      const bool holds =
          common(in.stations[s]).datums.height(d).has_value() or
          (reference and reference->computed_datums and
           reference->computed_datums->heights.height(d).has_value());
      if (not holds) {
        continue;
      }
      if (found) {
        ++found->stations;
      } else {
        found = DatumUnsupported{.datum = d, .stations = 1, .first_station = s};
      }
    }
    if (found) {
      out.emplace_back(*found);
    }
  }
}

void unused_constituents(const HarmonicsInput& in, const Resolution& r,
                         const detail::EntryLookup& lookup,
                         std::vector<HarmonicsWarning>& out) {
  std::vector<bool> used(r.entries.size(), false);
  for (const StationKindInput& station : in.stations) {
    if (const auto* reference = std::get_if<ReferenceInput>(&station)) {
      for (const ConstantInput& row : reference->constants) {
        if (const auto i = lookup.index(row.constituent.view())) {
          used[*i] = true;
        }
      }
    }
  }
  // A compound's terms are Darwin entries, so one pass marks them all.
  for (std::size_t i = 0; i < r.entries.size(); ++i) {
    const auto* compound = std::get_if<CompoundTerms>(&r.entries[i].definition);
    if (not used[i] or compound == nullptr) {
      continue;
    }
    for (const CompoundTerms::Term& t : compound->terms()) {
      if (const auto term = lookup.index(t.base.view())) {
        used[*term] = true;
      }
    }
  }
  for (std::size_t i = 0; i < used.size(); ++i) {
    if (not used[i]) {
      out.emplace_back(UnusedConstituent{.entry = i});
    }
  }
}

void unused_sources(const HarmonicsInput& in, const Resolution& r,
                    std::vector<HarmonicsWarning>& out) {
  std::vector<bool> used(in.sources.size(), false);
  for (const auto& source : r.station_source) {
    if (source) {
      used[*source] = true;
    }
  }
  for (std::size_t i = 0; i < used.size(); ++i) {
    if (not used[i]) {
      out.emplace_back(UnusedSource{.source = i});
    }
  }
}

std::vector<HarmonicsWarning> warnings(const HarmonicsInput& in,
                                       const Resolution& r,
                                       const detail::EntryLookup& lookup) {
  std::vector<HarmonicsWarning> out;
  redefined_constituents(r, out);
  unsupported_datums(in, out);
  unused_constituents(in, r, lookup, out);
  unused_sources(in, r, out);
  return out;
}

// ---------------------------------------------------------------------------
// The build, after every check passed.

/// Positions of `items` sorted by `key`: order[k] is the input position of
/// the k-th, and rank[input position] is its sorted position.
struct Sorted {
  std::vector<std::size_t> order;
  std::vector<std::size_t> rank;
};

/// The value a passed check stored. The build runs only after every check
/// passed, so an empty `o` is a bug in the checks: stop rather than build a
/// file from a guess.
template <class T>
T checked(std::optional<T> o) noexcept {
  if (not o) {
    std::abort();
  }
  return *std::move(o);
}

template <class T, class E>
T checked(std::expected<T, E> e) noexcept {
  if (not e) {
    std::abort();
  }
  return *std::move(e);
}

template <class Key>
Sorted sorted_by(std::size_t n, Key key) {
  Sorted s{.order = std::vector<std::size_t>(n),
           .rank = std::vector<std::size_t>(n)};
  for (std::size_t i = 0; i < n; ++i) {
    s.order[i] = i;
  }
  std::ranges::sort(s.order, std::less{}, key);
  for (std::size_t k = 0; k < n; ++k) {
    s.rank[s.order[k]] = k;
  }
  return s;
}

class Builder {
 public:
  Builder(HarmonicsInput& input, Resolution& r) : in_{input}, r_{r} {}

  HarmonicsData build() {
    const Sorted sources = sorted_by(in_.sources.size(), [this](std::size_t i) {
      return in_.sources[i].id.view();
    });
    source_rank_ = sources.rank;
    std::vector<Source> sorted_sources;
    sorted_sources.reserve(in_.sources.size());
    for (const std::size_t i : sources.order) {
      sorted_sources.push_back(source(i));
    }

    std::vector<std::size_t> references;
    std::vector<std::size_t> subordinates;
    for (std::size_t s = 0; s < in_.stations.size(); ++s) {
      (std::holds_alternative<ReferenceInput>(in_.stations[s]) ? references
                                                               : subordinates)
          .push_back(s);
    }
    const auto id_of = [this](std::size_t s) {
      return common(in_.stations[s]).id.value();
    };
    std::ranges::sort(references, std::less{}, id_of);
    std::ranges::sort(subordinates, std::less{}, id_of);
    reference_rank_.resize(in_.stations.size());
    for (std::size_t k = 0; k < references.size(); ++k) {
      reference_rank_[references[k]] = k;
    }

    HarmonicsData data{
        .header = header(),
        .sources = std::move(sorted_sources),
        .constituents = checked(ConstituentSet::make(std::move(r_.entries))),
        .references = {},
        .subordinates = {}};
    data.references.reserve(references.size());
    for (const std::size_t s : references) {
      data.references.push_back(reference(s, data.constituents));
    }
    data.subordinates.reserve(subordinates.size());
    for (const std::size_t s : subordinates) {
      data.subordinates.push_back(subordinate(s, data.references));
    }
    return data;
  }

 private:
  FileHeader header() {
    HeaderInput& h = in_.header;
    return FileHeader{.version = h.version,
                      .created = checked(r_.created),
                      .generator = std::move(h.generator),
                      .title = std::move(h.title),
                      .description = std::move(h.description),
                      .licence_summary = std::move(h.licence_summary)};
  }

  Source source(std::size_t i) {
    SourceInput& s = in_.sources[i];
    return Source{.id = s.id,
                  .name = std::move(s.name),
                  .url = std::move(s.url),
                  .licence = checked(std::move(r_.source_licences[i])),
                  .attribution = std::move(s.attribution),
                  .retrieved = checked(r_.retrieved[i]),
                  .version = std::move(s.version),
                  .sha256 = std::move(s.sha256),
                  .notes = std::move(s.notes)};
  }

  StationInfo info(std::size_t s, StationInput& station,
                   std::optional<ComputedDatums> computed) {
    return StationInfo{
        .id = std::move(station.id),
        .name = std::move(station.name),
        .location = station.location,
        .source = HarmonicsAccess::source(
            source_rank_[checked(r_.station_source[s])]),
        .licence = std::move(r_.station_licences[s]),
        .country = std::move(station.country),
        .time_zone = std::move(station.time_zone),
        .datums = checked(Datums::make(station.datums, computed,
                                       std::move(station.datum_epoch))),
        .record = r_.records[s],
        .flags = std::move(station.flags),
        .provenance = std::move(station.provenance)};
  }

  ReferenceRecord reference(std::size_t s, const ConstituentSet& set) {
    auto& input = std::get<ReferenceInput>(in_.stations[s]);
    std::optional<ComputedDatums> computed;
    if (input.computed_datums) {
      computed = ComputedDatums{.period = checked(r_.periods[s]),
                                .heights = input.computed_datums->heights};
    }
    std::vector<Constant> constants;
    constants.reserve(input.constants.size());
    for (const ConstantInput& row : input.constants) {
      constants.push_back(
          Constant{.constituent = checked(set.find(row.constituent.view())),
                   .value = row.value});
    }
    return ReferenceRecord{.info = info(s, input.station, computed),
                           .constants = std::move(constants),
                           .z0 = input.z0,
                           .analysis = input.analysis};
  }

  SubordinateRecord subordinate(
      std::size_t s, const std::vector<ReferenceRecord>& references) {
    auto& input = std::get<SubordinateInput>(in_.stations[s]);
    const std::size_t rank =
        reference_rank_[checked(r_.subordinate_reference[s])];
    return SubordinateRecord{
        .info = info(s, input.station, std::nullopt),
        .reference = HarmonicsAccess::reference(rank),
        .offsets = input.offsets,
        .reference_datum_height = checked(
            references[rank].info.datums.height(input.offsets.datum()))};
  }

  HarmonicsInput& in_;
  Resolution& r_;
  std::vector<std::size_t> source_rank_;
  std::vector<std::size_t> reference_rank_;
};

const ReferenceRecord& record_of(const HarmonicsData& d,
                                 ReferenceIndex i) noexcept {
  assert(i.value() < d.references.size());
  return d.references[i.value()];
}

const SubordinateRecord& record_of(const HarmonicsData& d,
                                   SubordinateIndex i) noexcept {
  assert(i.value() < d.subordinates.size());
  return d.subordinates[i.value()];
}

const Licence& effective(const HarmonicsData& d,
                         const StationInfo& info) noexcept {
  return info.licence ? *info.licence : d.sources[info.source.value()].licence;
}

}  // namespace

// ---------------------------------------------------------------------------
// Codes.

HarmonicsErrc code(const HarmonicsError& e) {
  using enum HarmonicsErrc;
  return std::visit(
      Overloaded{
          [](InvalidCreated) { return date; },
          [](DuplicateSource) { return duplicate_source; },
          [](InvalidSourceLicence) { return licence; },
          [](InvalidRetrieved) { return date; },
          [](const DuplicateName&) { return duplicate_constituent; },
          [](const InvalidTerm&) { return compound_term; },
          [](SpeedMismatch) { return speed_mismatch; },
          [](UnknownSource) { return unknown_source; },
          [](InvalidStationLicence) { return licence; },
          [](UnknownConstituent) { return unknown_constituent; },
          [](DuplicateRow) { return duplicate_row; },
          [](InvalidNumber) { return schema; },
          [](UncomputableDatum) { return schema; },
          [](DatumConflict) { return datum_conflict; },
          [](InvalidStationDate) { return date; },
          [](InvertedDates) { return date; },
          [](ShortDatumPeriod) { return datum_conflict; },
          [](DuplicateStation) { return duplicate_station; },
          [](ReferenceMissing) { return reference; },
          [](ReferenceNotReference) { return reference; },
          [](OffsetDatumMissing) { return offset_datum; },
      },
      e);
}

std::string_view to_string(HarmonicsErrc c) noexcept {
  switch (c) {
    case HarmonicsErrc::schema:
      return "E-SCHEMA";
    case HarmonicsErrc::date:
      return "E-DATE";
    case HarmonicsErrc::duplicate_source:
      return "E-DUPLICATE-SOURCE";
    case HarmonicsErrc::licence:
      return "E-LICENCE";
    case HarmonicsErrc::duplicate_constituent:
      return "E-DUPLICATE-CONSTITUENT";
    case HarmonicsErrc::compound_term:
      return "E-COMPOUND-TERM";
    case HarmonicsErrc::speed_mismatch:
      return "E-SPEED-MISMATCH";
    case HarmonicsErrc::unknown_source:
      return "E-UNKNOWN-SOURCE";
    case HarmonicsErrc::unknown_constituent:
      return "E-UNKNOWN-CONSTITUENT";
    case HarmonicsErrc::duplicate_row:
      return "E-DUPLICATE-ROW";
    case HarmonicsErrc::datum_conflict:
      return "E-DATUM-CONFLICT";
    case HarmonicsErrc::duplicate_station:
      return "E-DUPLICATE-STATION";
    case HarmonicsErrc::reference:
      return "E-REFERENCE";
    case HarmonicsErrc::offset_datum:
      return "E-OFFSET-DATUM";
  }
  return "E-SCHEMA";  // unreachable: every enumerator is handled
}

HarmonicsWarningCode code(const HarmonicsWarning& w) {
  using enum HarmonicsWarningCode;
  return std::visit(
      Overloaded{
          [](ConstituentRedefined) { return constituent_redefined; },
          [](UnusedConstituent) { return unused_constituent; },
          [](UnusedSource) { return unused_source; },
          [](DatumUnsupported) { return datum_unsupported; },
      },
      w);
}

std::string_view to_string(HarmonicsWarningCode c) noexcept {
  switch (c) {
    case HarmonicsWarningCode::constituent_redefined:
      return "W-CONSTITUENT-REDEFINED";
    case HarmonicsWarningCode::unused_constituent:
      return "W-UNUSED-CONSTITUENT";
    case HarmonicsWarningCode::unused_source:
      return "W-UNUSED-SOURCE";
    case HarmonicsWarningCode::datum_unsupported:
      return "W-DATUM-UNSUPPORTED";
  }
  return "W-DATUM-UNSUPPORTED";  // unreachable: every enumerator is handled
}

// ---------------------------------------------------------------------------
// make and validate.

std::expected<HarmonicsBuild, HarmonicsError> HarmonicsFile::make(
    HarmonicsInput input) {
  Checker checker{input, true};
  checker.run();
  if (not checker.errors().empty()) {
    return std::unexpected(checker.errors().front());
  }
  auto found = warnings(input, checker.resolution(), checker.lookup());
  auto data = Builder{input, checker.resolution()}.build();
  return HarmonicsBuild{
      .file = HarmonicsAccess::file(
          std::make_shared<const HarmonicsData>(std::move(data))),
      .warnings = std::move(found)};
}

HarmonicsReport validate(const HarmonicsInput& input) {
  Checker checker{input, false};
  checker.run();
  auto found = warnings(input, checker.resolution(), checker.lookup());
  return HarmonicsReport{.errors = std::move(checker.errors()),
                         .warnings = std::move(found)};
}

// ---------------------------------------------------------------------------
// The file's accessors.

const FileHeader& HarmonicsFile::header() const& noexcept {
  return data_->header;
}

std::span<const Source> HarmonicsFile::sources() const& noexcept {
  return data_->sources;
}

const Source& HarmonicsFile::source(SourceIndex i) const& noexcept {
  assert(i.value() < data_->sources.size());
  return data_->sources[i.value()];
}

const ConstituentSet& HarmonicsFile::constituents() const& noexcept {
  return data_->constituents;
}

std::size_t HarmonicsFile::reference_count() const noexcept {
  return data_->references.size();
}

std::size_t HarmonicsFile::subordinate_count() const noexcept {
  return data_->subordinates.size();
}

std::vector<ReferenceView> HarmonicsFile::references() const& {
  std::vector<ReferenceView> all;
  all.reserve(data_->references.size());
  for (std::size_t i = 0; i < data_->references.size(); ++i) {
    all.push_back(
        HarmonicsAccess::reference_view(*data_, HarmonicsAccess::reference(i)));
  }
  return all;
}

std::vector<SubordinateView> HarmonicsFile::subordinates() const& {
  std::vector<SubordinateView> all;
  all.reserve(data_->subordinates.size());
  for (std::size_t i = 0; i < data_->subordinates.size(); ++i) {
    all.push_back(HarmonicsAccess::subordinate_view(
        *data_, HarmonicsAccess::subordinate(i)));
  }
  return all;
}

ReferenceView HarmonicsFile::reference(ReferenceIndex i) const& noexcept {
  assert(i.value() < data_->references.size());
  return HarmonicsAccess::reference_view(*data_, i);
}

SubordinateView HarmonicsFile::subordinate(SubordinateIndex i) const& noexcept {
  assert(i.value() < data_->subordinates.size());
  return HarmonicsAccess::subordinate_view(*data_, i);
}

std::optional<StationIndex> HarmonicsFile::find(
    std::string_view id) const noexcept {
  const auto id_of = [](const auto& record) { return record.info.id.value(); };
  const auto& refs = data_->references;
  if (const auto r = std::ranges::lower_bound(refs, id, std::less{}, id_of);
      r != refs.end() and r->info.id.value() == id) {
    return StationIndex{
        HarmonicsAccess::reference(static_cast<std::size_t>(r - refs.begin()))};
  }
  const auto& subs = data_->subordinates;
  if (const auto s = std::ranges::lower_bound(subs, id, std::less{}, id_of);
      s != subs.end() and s->info.id.value() == id) {
    return StationIndex{HarmonicsAccess::subordinate(
        static_cast<std::size_t>(s - subs.begin()))};
  }
  return std::nullopt;
}

const StationInfo& HarmonicsFile::info(StationIndex i) const& {
  return std::visit(
      [this](auto index) -> const StationInfo& {
        return record_of(*data_, index).info;
      },
      i);
}

const Licence& HarmonicsFile::effective_licence(StationIndex i) const& {
  return effective(*data_, info(i));
}

bool operator==(const HarmonicsFile& a, const HarmonicsFile& b) {
  return a.data_ == b.data_ or *a.data_ == *b.data_;
}

// ---------------------------------------------------------------------------
// Views.

const StationInfo& ReferenceView::info() const noexcept {
  return record_of(*data_, index_).info;
}

const Licence& ReferenceView::licence() const noexcept {
  return effective(*data_, info());
}

const Source& ReferenceView::source() const noexcept {
  return data_->sources[info().source.value()];
}

const ConstituentSet& ReferenceView::constituents() const noexcept {
  return data_->constituents;
}

std::span<const Constant> ReferenceView::constants() const noexcept {
  return record_of(*data_, index_).constants;
}

Length ReferenceView::z0() const noexcept {
  return record_of(*data_, index_).z0;
}

std::optional<Analysis> ReferenceView::analysis() const noexcept {
  return record_of(*data_, index_).analysis;
}

const StationInfo& SubordinateView::info() const noexcept {
  return record_of(*data_, index_).info;
}

const Licence& SubordinateView::licence() const noexcept {
  return effective(*data_, info());
}

const Source& SubordinateView::source() const noexcept {
  return data_->sources[info().source.value()];
}

ReferenceView SubordinateView::reference() const noexcept {
  return HarmonicsAccess::reference_view(*data_,
                                         record_of(*data_, index_).reference);
}

const SubordinateOffsets& SubordinateView::offsets() const noexcept {
  return record_of(*data_, index_).offsets;
}

Length SubordinateView::reference_datum_height() const noexcept {
  return record_of(*data_, index_).reference_datum_height;
}

}  // namespace mov::core::tide
