// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/tide/harmonics.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <expected>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/detail/numeric.hpp"
#include "mov/core/station.hpp"
#include "mov/core/tide/builtin.hpp"
#include "mov/core/tide/constituent.hpp"
#include "mov/core/tide/constituent_set.hpp"
#include "mov/core/tide/datums.hpp"
#include "mov/core/tide/harmonics_input.hpp"
#include "mov/core/tide/licence.hpp"
#include "mov/core/tide/positions.hpp"
#include "mov/core/tide/source.hpp"
#include "mov/core/tide/station_info.hpp"
#include "mov/core/tide/year_arguments.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"

namespace mov::core::detail {

struct ReferenceRecord {
  tide::StationInfo info;
  std::vector<tide::Constant> constants;  // sorted by constituent
  Length z0;
  std::optional<tide::Analysis> analysis;
  friend bool operator==(const ReferenceRecord&,
                         const ReferenceRecord&) = default;
};

struct SubordinateRecord {
  tide::StationInfo info;
  tide::ReferenceIndex reference;
  tide::SubordinateOffsets offsets;
  Length reference_datum_height;  // the reference's height of offsets.datum
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

/// The one friend of the file's index, view and set types: makes them from
/// positions the checks resolved.
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
  static tide::ConstituentSet::Ranked ranked(
      std::vector<tide::DictionaryEntry> entries,
      std::span<const tide::Argument> arguments) {
    return tide::ConstituentSet::ranked(std::move(entries), arguments);
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

std::string_view id_of(const StationKindInput& s) {
  return common(s).id.value();
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

/// The value of a fragment of a Checked without errors: every fragment is
/// engaged then (an item's fragment is engaged when all its checks passed).
template <class T>
T engaged(std::optional<T> o) noexcept {
  assert(o.has_value());
  return *std::move(o);
}

/// A check failed (and was reported) or was skipped because one it depends
/// on failed.
struct Failed {};

struct ResolvedSource {
  Licence licence;
  std::chrono::sys_days retrieved;
};

struct ResolvedStation {
  std::optional<Licence> licence;  // the station's override, if it has one
  std::optional<Record> record;    // if it has one
  Datums datums;
};

/// A subordinate's reference, resolved.
struct Link {
  std::size_t reference;  // input position of the reference station
  Length datum_height;    // the reference's height of offsets.datum
};

/// What the checks of a decoded file found: the errors in the pinned order,
/// the lookups (made whatever the checks say), and each item resolved,
/// engaged when every check of that item passed.
struct Checked {
  std::vector<HarmonicsError> errors;

  // Lookups.
  std::vector<DictionaryEntry> entries;  // input order
  detail::EntryIndex entry_index;
  std::vector<std::optional<std::size_t>> station_sources;    // by namespace
  std::vector<std::vector<std::optional<std::size_t>>> rows;  // row -> entry

  // Resolved items.
  std::optional<Time> created;
  std::vector<std::optional<ResolvedSource>> sources;
  std::vector<std::optional<Argument>> arguments;
  std::vector<std::optional<ResolvedStation>> stations;
  std::vector<std::optional<Link>> links;  // subordinates only
};

std::vector<DictionaryEntry> entries_of(const HarmonicsInput& in) {
  std::vector<DictionaryEntry> out;
  out.reserve(in.constituents.size());
  std::ranges::transform(in.constituents, std::back_inserter(out),
                         &ConstituentInput::entry);
  return out;
}

/// Every check of HarmonicsFile::make, in the pinned order
/// (docs/harmonics-engine.md 2.7). A check that depends on a failed one is
/// skipped; nothing else stops the run.
class Checker {
 public:
  explicit Checker(const HarmonicsInput& input)
      : in_{input}, c_{initial(input)} {}

  [[nodiscard]] Checked run() && {
    check_created();
    check_sources();
    check_dictionary();
    for (std::size_t s = 0; s < in_.stations.size(); ++s) {
      check_station(s);
    }
    check_cross();
    return std::move(c_);
  }

 private:
  static Checked initial(const HarmonicsInput& in) {
    auto entries = entries_of(in);
    detail::EntryIndex index{entries};
    const std::size_t stations = in.stations.size();
    return Checked{
        .errors = {},
        .entries = std::move(entries),
        .entry_index = std::move(index),
        .station_sources = std::vector<std::optional<std::size_t>>(stations),
        .rows = std::vector<std::vector<std::optional<std::size_t>>>(stations),
        .created = std::nullopt,
        .sources =
            std::vector<std::optional<ResolvedSource>>(in.sources.size()),
        .arguments =
            std::vector<std::optional<Argument>>(in.constituents.size()),
        .stations = std::vector<std::optional<ResolvedStation>>(stations),
        .links = std::vector<std::optional<Link>>(stations)};
  }

  void report(HarmonicsError e) { c_.errors.push_back(e); }

  void check_created() {
    c_.created = timestamp(in_.header.created);
    if (not c_.created) {
      report(InvalidCreated{});
    }
  }

  void check_sources() {
    const auto first = detail::first_occurrence(
        in_.sources.size(),
        [this](std::size_t i) { return in_.sources[i].id.view(); });
    for (std::size_t i = 0; i < in_.sources.size(); ++i) {
      const SourceInput& s = in_.sources[i];
      if (const auto& earlier = first[i]) {
        report(DuplicateSource{.source = i, .first = *earlier});
      }
      auto licence = Licence::parse(s.licence);
      if (not licence) {
        report(InvalidSourceLicence{.source = i, .error = licence.error()});
      }
      const auto retrieved = calendar_date(s.retrieved);
      if (not retrieved) {
        report(InvalidRetrieved{.source = i});
      }
      if (not first[i] and licence and retrieved) {
        c_.sources[i] = ResolvedSource{.licence = *std::move(licence),
                                       .retrieved = *retrieved};
      }
    }
  }

  void check_dictionary() {
    const auto first = detail::same_name_ignoring_case(c_.entries);
    for (std::size_t i = 0; i < c_.entries.size(); ++i) {
      if (const auto& earlier = first[i]) {
        report(DuplicateName{.entry = i, .first = *earlier});
      }
      const auto argument =
          flatten(c_.entries[i].definition, c_.entry_index.lookup(c_.entries));
      if (not argument) {
        report(InvalidTerm{.entry = i, .error = argument.error()});
        continue;  // no speed without a definition
      }
      const double derived = argument->speed_deg_per_hour();
      const bool speed_ok =
          std::abs(in_.constituents[i].speed - derived) <= max_speed_difference;
      if (not speed_ok) {
        report(SpeedMismatch{.entry = i, .derived = derived});
      }
      if (not first[i] and speed_ok) {
        c_.arguments[i] = *argument;
      }
    }
  }

  void check_station(std::size_t s) {
    const StationInput& info = common(in_.stations[s]);
    const auto* reference = std::get_if<ReferenceInput>(&in_.stations[s]);
    const bool known_source = check_namespace(s, info);
    auto licence = check_licence(s, info);
    const bool rows = reference == nullptr or check_rows(s, *reference);
    const bool z0 = reference == nullptr or check_z0(s, *reference);
    const bool years = check_record_years(s, info);
    const auto record = check_record(s, info);
    const auto computed = check_period(s, reference);
    // Conflicts are checked only with a valid period (dependent).
    auto datums =
        computed ? check_datums(s, info, *computed)
                 : std::expected<Datums, Failed>{std::unexpected(Failed{})};
    if (known_source and licence and rows and z0 and years and record and
        datums) {
      c_.stations[s] = ResolvedStation{.licence = *std::move(licence),
                                       .record = *record,
                                       .datums = *std::move(datums)};
    }
  }

  bool check_namespace(std::size_t s, const StationInput& info) {
    const SourceToken ns = namespace_of(info.id);
    const auto source = std::ranges::find(in_.sources, ns, &SourceInput::id);
    if (source == in_.sources.end()) {
      report(UnknownSource{.station = s});
      return false;
    }
    c_.station_sources[s] =
        static_cast<std::size_t>(source - in_.sources.begin());
    return true;
  }

  std::expected<std::optional<Licence>, Failed> check_licence(
      std::size_t s, const StationInput& info) {
    if (not info.licence) {
      return std::optional<Licence>{};
    }
    auto licence = Licence::parse(*info.licence);
    if (not licence) {
      report(InvalidStationLicence{.station = s, .error = licence.error()});
      return std::unexpected(Failed{});
    }
    return std::optional<Licence>{*std::move(licence)};
  }

  /// Each row names an entry (recorded once, for everyone), then no row
  /// repeats an earlier row's entry.
  bool check_rows(std::size_t s, const ReferenceInput& reference) {
    auto& rows = c_.rows[s];
    rows.reserve(reference.constants.size());
    std::ranges::transform(reference.constants, std::back_inserter(rows),
                           [this](const ConstantInput& row) {
                             return c_.entry_index.find(c_.entries,
                                                        row.constituent.view());
                           });
    const auto first = detail::first_occurrence(
        rows.size(), [&rows](std::size_t r) { return rows[r]; });
    for (std::size_t row = 0; row < rows.size(); ++row) {
      if (not rows[row]) {
        report(UnknownConstituent{.station = s, .row = row});
      } else if (const auto& earlier = first[row]) {
        report(DuplicateRow{.station = s, .row = row, .first = *earlier});
      }
    }
    return std::ranges::all_of(rows,
                               [](const auto& e) { return e.has_value(); }) and
           std::ranges::none_of(first,
                                [](const auto& f) { return f.has_value(); });
  }

  bool check_z0(std::size_t s, const ReferenceInput& reference) {
    if (detail::is_finite(reference.z0.as(LengthUnit::meter))) {
      return true;
    }
    report(InvalidNumber{.station = s, .number = StationNumber::z0});
    return false;
  }

  bool check_record_years(std::size_t s, const StationInput& info) {
    const auto years = info.record ? info.record->years : std::nullopt;
    if (not years or (detail::is_finite(*years) and *years > 0.0)) {
      return true;
    }
    report(InvalidNumber{.station = s, .number = StationNumber::record_years});
    return false;
  }

  /// The calendar date of `d`; reports it when it is none.
  std::optional<std::chrono::sys_days> date(
      std::size_t s, const std::chrono::year_month_day& d, StationDate which) {
    const auto day = calendar_date(d);
    if (not day) {
      report(InvalidStationDate{.station = s, .date = which});
    }
    return day;
  }

  std::expected<std::optional<Record>, Failed> check_record(
      std::size_t s, const StationInput& info) {
    if (not info.record) {
      return std::optional<Record>{};
    }
    const RecordInput& r = *info.record;
    // Absent dates are unknown; present ones must be calendar dates.
    const auto start =
        r.start ? date(s, *r.start, StationDate::record_start) : std::nullopt;
    const auto end =
        r.end ? date(s, *r.end, StationDate::record_end) : std::nullopt;
    if ((r.start and not start) or (r.end and not end)) {
      return std::unexpected(Failed{});  // dependent: no order without dates
    }
    const auto dates = ValidRange::make({.first = start, .last = end});
    if (not dates) {
      report(InvertedDates{.station = s, .range = DateRange::record});
      return std::unexpected(Failed{});
    }
    return std::optional<Record>{Record{.dates = *dates, .years = r.years}};
  }

  /// The computed datums with their period, if the station has them.
  std::expected<std::optional<ComputedDatums>, Failed> check_period(
      std::size_t s, const ReferenceInput* reference) {
    if (reference == nullptr or not reference->computed_datums) {
      return std::optional<ComputedDatums>{};
    }
    const ComputedDatumsInput& computed = *reference->computed_datums;
    const auto start = date(s, computed.start, StationDate::period_start);
    const auto end = date(s, computed.end, StationDate::period_end);
    if (not start or not end) {
      return std::unexpected(Failed{});  // dependent: no order without dates
    }
    const auto period = DatumPeriod::make(*start, *end);
    if (not period) {
      report(period.error() == DatumPeriodError::inverted
                 ? HarmonicsError{InvertedDates{.station = s,
                                                .range = DateRange::period}}
                 : HarmonicsError{ShortDatumPeriod{.station = s}});
      return std::unexpected(Failed{});
    }
    return std::optional<ComputedDatums>{
        ComputedDatums{.period = *period, .heights = computed.heights}};
  }

  std::expected<Datums, Failed> check_datums(
      std::size_t s, const StationInput& info,
      const std::optional<ComputedDatums>& computed) {
    auto datums = Datums::make(info.datums, computed, info.datum_epoch);
    if (not datums) {
      for (const VerticalDatum d : detail::all_datums) {
        if (datums.error().datums.contains(d)) {
          report(DatumConflict{.station = s, .datum = d});
        }
      }
      return std::unexpected(Failed{});
    }
    return *std::move(datums);
  }

  void check_cross() {
    const std::size_t n = in_.stations.size();
    const auto id = [this](std::size_t s) { return id_of(in_.stations[s]); };
    const auto first = detail::first_occurrence(n, id);
    for (std::size_t s = 0; s < n; ++s) {
      if (const auto& earlier = first[s]) {
        report(DuplicateStation{.station = s, .first = *earlier});
      }
    }
    const std::vector<std::size_t> by_id = detail::sorted_index(n, id);
    for (std::size_t s = 0; s < n; ++s) {
      if (const auto* sub = std::get_if<SubordinateInput>(&in_.stations[s])) {
        check_reference(s, *sub, by_id, id);
      }
    }
  }

  /// The reference exists and is a reference station; then it knows the
  /// offsets' datum. The second is skipped when the reference's own checks
  /// failed (its datums did not resolve).
  void check_reference(std::size_t s, const SubordinateInput& sub,
                       const std::vector<std::size_t>& by_id, const auto& id) {
    const auto found = detail::find_sorted(by_id, sub.reference.value(), id);
    if (not found) {
      report(ReferenceMissing{.station = s});
      return;
    }
    if (not std::holds_alternative<ReferenceInput>(in_.stations[*found])) {
      report(ReferenceNotReference{.station = s, .referenced = *found});
      return;
    }
    const auto& reference = c_.stations[*found];
    if (not reference) {
      return;
    }
    const VerticalDatum d = sub.offsets.datum();
    const auto height = reference->datums.height(d);
    if (not height) {
      report(OffsetDatumMissing{.station = s, .datum = d});
      return;
    }
    c_.links[s] = Link{.reference = *found, .datum_height = *height};
  }

  const HarmonicsInput& in_;
  Checked c_;
};

// ---------------------------------------------------------------------------
// Warnings.

void redefined_constituents(const Checked& c,
                            std::vector<HarmonicsWarning>& out) {
  for (std::size_t i = 0; i < c.entries.size(); ++i) {
    const auto& argument = c.arguments[i];
    if (not argument) {
      continue;
    }
    const auto builtin = builtin_argument(c.entries[i].name.view());
    if (builtin and *builtin != *argument) {
      out.emplace_back(ConstituentRedefined{.entry = i});
    }
  }
}

void unused_constituents(const Checked& c, std::vector<HarmonicsWarning>& out) {
  std::vector<bool> used(c.entries.size(), false);
  for (const auto& rows : c.rows) {
    for (const auto& entry : rows) {
      if (entry) {
        used[*entry] = true;
      }
    }
  }
  // A compound's terms are Darwin entries, so one pass marks them all.
  for (std::size_t i = 0; i < c.entries.size(); ++i) {
    const auto* compound = std::get_if<CompoundTerms>(&c.entries[i].definition);
    if (not used[i] or compound == nullptr) {
      continue;
    }
    for (const CompoundTerms::Term& t : compound->terms()) {
      if (const auto term = c.entry_index.find(c.entries, t.base.view())) {
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

void unused_sources(std::size_t sources, const Checked& c,
                    std::vector<HarmonicsWarning>& out) {
  std::vector<bool> used(sources, false);
  for (const auto& source : c.station_sources) {
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
                                       const Checked& c) {
  std::vector<HarmonicsWarning> out;
  redefined_constituents(c, out);
  unused_constituents(c, out);
  unused_sources(in.sources.size(), c, out);
  return out;
}

// ---------------------------------------------------------------------------
// The build, from a Checked without errors.

/// rank[order[k]] = k.
std::vector<std::size_t> ranks(std::span<const std::size_t> order,
                               std::size_t n) {
  std::vector<std::size_t> rank(n);
  for (std::size_t k = 0; k < order.size(); ++k) {
    rank[order[k]] = k;
  }
  return rank;
}

class Builder {
 public:
  Builder(HarmonicsInput& input, Checked& checked) : in_{input}, c_{checked} {}

  [[nodiscard]] HarmonicsData build() && {
    const std::vector<std::size_t> source_order = detail::sorted_index(
        in_.sources.size(),
        [this](std::size_t i) { return in_.sources[i].id.view(); });
    source_rank_ = ranks(source_order, in_.sources.size());
    std::vector<Source> sources;
    sources.reserve(source_order.size());
    std::ranges::transform(source_order, std::back_inserter(sources),
                           [this](std::size_t i) { return source(i); });

    std::vector<Argument> arguments;
    arguments.reserve(c_.arguments.size());
    std::ranges::transform(c_.arguments, std::back_inserter(arguments),
                           [](const auto& a) { return engaged(a); });
    auto ranked = HarmonicsAccess::ranked(std::move(c_.entries), arguments);
    entry_index_ = std::move(ranked.index_of);

    // Stations in id order, split by kind (each part stays in id order).
    const std::size_t n = in_.stations.size();
    const std::vector<std::size_t> by_id = detail::sorted_index(
        n, [this](std::size_t s) { return id_of(in_.stations[s]); });
    std::vector<std::size_t> references;
    std::vector<std::size_t> subordinates;
    std::ranges::partition_copy(
        by_id, std::back_inserter(references), std::back_inserter(subordinates),
        [this](std::size_t s) {
          return std::holds_alternative<ReferenceInput>(in_.stations[s]);
        });
    reference_rank_ = ranks(references, n);

    HarmonicsData data{.header = header(),
                       .sources = std::move(sources),
                       .constituents = std::move(ranked.set),
                       .references = {},
                       .subordinates = {}};
    data.references.reserve(references.size());
    std::ranges::transform(references, std::back_inserter(data.references),
                           [this](std::size_t s) { return reference(s); });
    data.subordinates.reserve(subordinates.size());
    std::ranges::transform(subordinates, std::back_inserter(data.subordinates),
                           [this](std::size_t s) { return subordinate(s); });
    return data;
  }

 private:
  FileHeader header() {
    HeaderInput& h = in_.header;
    return FileHeader{.version = h.version,
                      .created = engaged(c_.created),
                      .generator = std::move(h.generator),
                      .title = std::move(h.title),
                      .description = std::move(h.description),
                      .licence_summary = std::move(h.licence_summary)};
  }

  Source source(std::size_t i) {
    SourceInput& s = in_.sources[i];
    ResolvedSource resolved = engaged(std::move(c_.sources[i]));
    return Source{.id = s.id,
                  .name = std::move(s.name),
                  .url = std::move(s.url),
                  .licence = std::move(resolved.licence),
                  .attribution = std::move(s.attribution),
                  .retrieved = resolved.retrieved,
                  .version = std::move(s.version),
                  .sha256 = std::move(s.sha256),
                  .notes = std::move(s.notes)};
  }

  StationInfo info(std::size_t s, StationInput& station) {
    ResolvedStation resolved = engaged(std::move(c_.stations[s]));
    return StationInfo{.id = std::move(station.id),
                       .name = std::move(station.name),
                       .location = station.location,
                       .source = HarmonicsAccess::source(
                           source_rank_[engaged(c_.station_sources[s])]),
                       .licence = std::move(resolved.licence),
                       .country = std::move(station.country),
                       .time_zone = std::move(station.time_zone),
                       .datums = std::move(resolved.datums),
                       .record = resolved.record,
                       .flags = std::move(station.flags),
                       .provenance = std::move(station.provenance)};
  }

  ReferenceRecord reference(std::size_t s) {
    auto& input = std::get<ReferenceInput>(in_.stations[s]);
    const auto& rows = c_.rows[s];
    std::vector<Constant> constants;
    constants.reserve(rows.size());
    for (std::size_t r = 0; r < rows.size(); ++r) {
      constants.push_back(
          Constant{.constituent = entry_index_[engaged(rows[r])],
                   .value = input.constants[r].value});
    }
    std::ranges::sort(constants, std::less{}, &Constant::constituent);
    return ReferenceRecord{.info = info(s, input.station),
                           .constants = std::move(constants),
                           .z0 = input.z0,
                           .analysis = input.analysis};
  }

  SubordinateRecord subordinate(std::size_t s) {
    auto& input = std::get<SubordinateInput>(in_.stations[s]);
    const Link link = engaged(c_.links[s]);
    return SubordinateRecord{.info = info(s, input.station),
                             .reference = HarmonicsAccess::reference(
                                 reference_rank_[link.reference]),
                             .offsets = input.offsets,
                             .reference_datum_height = link.datum_height};
  }

  HarmonicsInput& in_;
  Checked& c_;
  std::vector<std::size_t> source_rank_;
  std::vector<std::size_t> reference_rank_;
  std::vector<ConstituentIndex> entry_index_;  // by input entry position
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
// make and validate: one checker, so make's error is validate's first.

HarmonicsReport validate(const HarmonicsInput& input) {
  Checked checked = Checker{input}.run();
  auto found = warnings(input, checked);
  return HarmonicsReport{.errors = std::move(checked.errors),
                         .warnings = std::move(found)};
}

std::expected<HarmonicsBuild, HarmonicsError> HarmonicsFile::make(
    HarmonicsInput input) {
  Checked checked = Checker{input}.run();
  if (not checked.errors.empty()) {
    return std::unexpected(checked.errors.front());
  }
  auto found = warnings(input, checked);
  auto data = Builder{input, checked}.build();
  return HarmonicsBuild{
      .file = HarmonicsAccess::file(
          std::make_shared<const HarmonicsData>(std::move(data))),
      .warnings = std::move(found)};
}

std::expected<YearArguments, YearOutOfRange> year_arguments(
    const HarmonicsFile& file, std::chrono::year y) {
  return year_arguments(file.constituents(), y);
}

std::expected<YearArguments, YearOutOfRange> year_arguments(
    const ReferenceView& station, std::chrono::year y) {
  return year_arguments(station.constituents(), y);
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

ReferenceView HarmonicsFile::reference(ReferenceIndex i) const& noexcept {
  assert(i.value() < data_->references.size());
  return ReferenceView{*data_, i};
}

SubordinateView HarmonicsFile::subordinate(SubordinateIndex i) const& noexcept {
  assert(i.value() < data_->subordinates.size());
  return SubordinateView{*data_, i};
}

std::optional<StationIndex> HarmonicsFile::find(
    std::string_view id) const noexcept {
  const auto id_in = [](const auto& record) { return record.info.id.value(); };
  const auto& refs = data_->references;
  if (const auto r = std::ranges::lower_bound(refs, id, {}, id_in);
      r != refs.end() and r->info.id.value() == id) {
    return StationIndex{
        ReferenceIndex{static_cast<std::size_t>(r - refs.begin())}};
  }
  const auto& subs = data_->subordinates;
  if (const auto s = std::ranges::lower_bound(subs, id, {}, id_in);
      s != subs.end() and s->info.id.value() == id) {
    return StationIndex{
        SubordinateIndex{static_cast<std::size_t>(s - subs.begin())}};
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

std::optional<HarmonicConstant> ReferenceView::constant(
    ConstituentIndex i) const noexcept {
  const auto all = constants();
  const auto found =
      std::ranges::lower_bound(all, i, {}, &Constant::constituent);
  if (found == all.end() or found->constituent != i) {
    return std::nullopt;
  }
  return found->value;
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
