// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/overloaded.hpp"
#include "mov/core/tide/constituent.hpp"
#include "mov/core/tide/constituent_set.hpp"
#include "mov/core/tide/datums.hpp"
#include "mov/core/tide/harmonics_input.hpp"
#include "mov/core/tide/licence.hpp"
#include "mov/core/tide/source.hpp"
#include "mov/core/tide/station_info.hpp"
#include "mov/core/tide/year_arguments.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/core/version.hpp"

// A harmonics file in memory (docs/harmonics-engine.md 2.5 and 2.7): built
// once from the decoded input by HarmonicsFile::make, which checks every
// rule of docs/harmonics-json.md 10.1 that needs more than one value and
// resolves every name and id exactly once. Prediction reads it only through
// ReferenceView and SubordinateView.

namespace mov::core::detail {
struct HarmonicsData;  // the file's records (src/core/tide/harmonics.cpp)
}  // namespace mov::core::detail

namespace mov::core::tide {

/// The position of a reference station in its file (sorted by id). Only the
/// file makes one.
class ReferenceIndex {
 public:
  [[nodiscard]] constexpr std::size_t value() const noexcept { return v_; }
  friend constexpr auto operator<=>(ReferenceIndex, ReferenceIndex) = default;

 private:
  friend class HarmonicsFile;
  friend struct detail::HarmonicsAccess;
  explicit constexpr ReferenceIndex(std::size_t v) noexcept : v_{v} {}
  std::size_t v_;
};

/// The position of a subordinate station in its file (sorted by id). Only
/// the file makes one.
class SubordinateIndex {
 public:
  [[nodiscard]] constexpr std::size_t value() const noexcept { return v_; }
  friend constexpr auto operator<=>(SubordinateIndex,
                                    SubordinateIndex) = default;

 private:
  friend class HarmonicsFile;
  friend struct detail::HarmonicsAccess;
  explicit constexpr SubordinateIndex(std::size_t v) noexcept : v_{v} {}
  std::size_t v_;
};
static_assert(std::is_trivially_copyable_v<ReferenceIndex>);
static_assert(std::is_trivially_copyable_v<SubordinateIndex>);

/// A station of either kind.
using StationIndex = std::variant<ReferenceIndex, SubordinateIndex>;

/// The top-level members a file keeps (docs/harmonics-json.md section 4).
struct FileHeader {
  VersionNumber version;
  Time created;
  Generator generator;
  std::optional<std::string> title;
  std::optional<std::string> description;
  std::string licence_summary;
  friend bool operator==(const FileHeader&, const FileHeader&) = default;
};

// ---------------------------------------------------------------------------
// Errors. Each names positions in the HarmonicsInput vectors (document
// order), from which the reader builds its JSON pointer. code() gives the
// docs/harmonics-json.md 10.1 code.

/// Which date of a station.
enum class StationDate : std::uint8_t {
  record_start,
  record_end,
  period_start,  // computed_datums.period.start
  period_end
};
/// Which pair of a station's dates.
enum class DateRange : std::uint8_t { record, period };
/// Which number of a station.
enum class StationNumber : std::uint8_t { z0, record_years };

/// `created` is not a valid timestamp (E-DATE).
struct InvalidCreated {
  friend constexpr bool operator==(InvalidCreated, InvalidCreated) = default;
};
/// sources[source].retrieved is not a calendar date (E-DATE).
struct InvalidRetrieved {
  std::size_t source;
  friend constexpr bool operator==(InvalidRetrieved,
                                   InvalidRetrieved) = default;
};
/// sources[source].id equals sources[first].id (E-DUPLICATE-SOURCE).
struct DuplicateSource {
  std::size_t source;
  std::size_t first;
  friend constexpr bool operator==(DuplicateSource, DuplicateSource) = default;
};
/// sources[source].licence does not parse (E-LICENCE).
struct InvalidSourceLicence {
  std::size_t source;
  LicenceError error;
  friend constexpr bool operator==(InvalidSourceLicence,
                                   InvalidSourceLicence) = default;
};
/// constituents[entry].speed differs from the derived speed by more than
/// 1e-5 deg/h (E-SPEED-MISMATCH).
struct SpeedMismatch {
  std::size_t entry;
  double derived;
  friend constexpr bool operator==(SpeedMismatch, SpeedMismatch) = default;
};
/// stations[station].id's namespace is not a source id (E-UNKNOWN-SOURCE).
struct UnknownSource {
  std::size_t station;
  friend constexpr bool operator==(UnknownSource, UnknownSource) = default;
};
/// stations[station].licence does not parse (E-LICENCE).
struct InvalidStationLicence {
  std::size_t station;
  LicenceError error;
  friend constexpr bool operator==(InvalidStationLicence,
                                   InvalidStationLicence) = default;
};
/// stations[station].constituents[row] names no dictionary entry
/// (E-UNKNOWN-CONSTITUENT).
struct UnknownConstituent {
  std::size_t station;
  std::size_t row;
  friend constexpr bool operator==(UnknownConstituent,
                                   UnknownConstituent) = default;
};
/// stations[station].constituents[row] names the constituent of row `first`
/// (E-DUPLICATE-ROW).
struct DuplicateRow {
  std::size_t station;
  std::size_t row;
  std::size_t first;
  friend constexpr bool operator==(DuplicateRow, DuplicateRow) = default;
};
/// z0 is not finite, or record years not finite and > 0 (E-SCHEMA). The
/// reader's schema step refuses them first; this keeps make() total.
struct InvalidNumber {
  std::size_t station;
  StationNumber number;
  friend constexpr bool operator==(InvalidNumber, InvalidNumber) = default;
};
/// `datum` is in both `datums` and `computed_datums.heights`
/// (E-DATUM-CONFLICT).
struct DatumConflict {
  std::size_t station;
  VerticalDatum datum;
  friend constexpr bool operator==(DatumConflict, DatumConflict) = default;
};
/// A station date is not a calendar date (E-DATE).
struct InvalidStationDate {
  std::size_t station;
  StationDate date;
  friend constexpr bool operator==(InvalidStationDate,
                                   InvalidStationDate) = default;
};
/// A station's end date precedes its start (E-DATE).
struct InvertedDates {
  std::size_t station;
  DateRange range;
  friend constexpr bool operator==(InvertedDates, InvertedDates) = default;
};
/// The computed period spans less than 18.61 years (E-DATUM-CONFLICT).
struct ShortDatumPeriod {
  std::size_t station;
  friend constexpr bool operator==(ShortDatumPeriod,
                                   ShortDatumPeriod) = default;
};
/// stations[station].id equals stations[first].id (E-DUPLICATE-STATION).
struct DuplicateStation {
  std::size_t station;
  std::size_t first;
  friend constexpr bool operator==(DuplicateStation,
                                   DuplicateStation) = default;
};
/// A subordinate's offsets.reference names no station of the file
/// (E-REFERENCE).
struct ReferenceMissing {
  std::size_t station;
  friend constexpr bool operator==(ReferenceMissing,
                                   ReferenceMissing) = default;
};
/// A subordinate's offsets.reference names stations[referenced], another
/// subordinate (E-REFERENCE).
struct ReferenceNotReference {
  std::size_t station;
  std::size_t referenced;
  friend constexpr bool operator==(ReferenceNotReference,
                                   ReferenceNotReference) = default;
};
/// A subordinate's reference knows neither a published nor a computed
/// height of the offsets' datum (E-OFFSET-DATUM).
struct OffsetDatumMissing {
  std::size_t station;
  VerticalDatum datum;
  friend constexpr bool operator==(OffsetDatumMissing,
                                   OffsetDatumMissing) = default;
};

/// Every way a decoded file can fail to resolve. DuplicateName and
/// InvalidTerm are the dictionary's (their `entry` indexes `constituents`).
using HarmonicsError =
    std::variant<InvalidCreated, DuplicateSource, InvalidSourceLicence,
                 InvalidRetrieved, DuplicateName, InvalidTerm, SpeedMismatch,
                 UnknownSource, InvalidStationLicence, UnknownConstituent,
                 DuplicateRow, InvalidNumber, InvalidStationDate, InvertedDates,
                 ShortDatumPeriod, DatumConflict, DuplicateStation,
                 ReferenceMissing, ReferenceNotReference, OffsetDatumMissing>;

/// The docs/harmonics-json.md 10.1 codes HarmonicsFile::make reports.
enum class HarmonicsErrc : std::uint8_t {
  schema,
  date,
  duplicate_source,
  licence,
  duplicate_constituent,
  compound_term,
  speed_mismatch,
  unknown_source,
  unknown_constituent,
  duplicate_row,
  datum_conflict,
  duplicate_station,
  reference,
  offset_datum
};

[[nodiscard]] constexpr HarmonicsErrc code(const HarmonicsError& e) {
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
          [](InvalidStationDate) { return date; },
          [](InvertedDates) { return date; },
          [](ShortDatumPeriod) { return datum_conflict; },
          [](DatumConflict) { return datum_conflict; },
          [](DuplicateStation) { return duplicate_station; },
          [](ReferenceMissing) { return reference; },
          [](ReferenceNotReference) { return reference; },
          [](OffsetDatumMissing) { return offset_datum; },
      },
      e);
}

/// "E-SCHEMA", "E-DATE", ... A switch without default.
[[nodiscard]] constexpr std::string_view to_string(HarmonicsErrc c) noexcept {
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

// ---------------------------------------------------------------------------
// Warnings (docs/harmonics-json.md 10.2); the file is accepted.

/// constituents[entry] means something other than the built-in catalogue's
/// entry of that name or alias: its flattened Argument (V, c, u and the
/// node-factor exponents) differs. The file's definition is used.
struct ConstituentRedefined {
  std::size_t entry;
  friend constexpr bool operator==(ConstituentRedefined,
                                   ConstituentRedefined) = default;
};
/// No reference row names constituents[entry], nor a term of a compound
/// that one names.
struct UnusedConstituent {
  std::size_t entry;
  friend constexpr bool operator==(UnusedConstituent,
                                   UnusedConstituent) = default;
};
/// No station id is in the namespace of sources[source].
struct UnusedSource {
  std::size_t source;
  friend constexpr bool operator==(UnusedSource, UnusedSource) = default;
};

using HarmonicsWarning =
    std::variant<ConstituentRedefined, UnusedConstituent, UnusedSource>;

enum class HarmonicsWarningCode : std::uint8_t {
  constituent_redefined,
  unused_constituent,
  unused_source
};

[[nodiscard]] constexpr HarmonicsWarningCode code(const HarmonicsWarning& w) {
  using enum HarmonicsWarningCode;
  return std::visit(
      Overloaded{
          [](ConstituentRedefined) { return constituent_redefined; },
          [](UnusedConstituent) { return unused_constituent; },
          [](UnusedSource) { return unused_source; },
      },
      w);
}

/// "W-CONSTITUENT-REDEFINED", ... A switch without default.
[[nodiscard]] constexpr std::string_view to_string(
    HarmonicsWarningCode c) noexcept {
  switch (c) {
    case HarmonicsWarningCode::constituent_redefined:
      return "W-CONSTITUENT-REDEFINED";
    case HarmonicsWarningCode::unused_constituent:
      return "W-UNUSED-CONSTITUENT";
    case HarmonicsWarningCode::unused_source:
      return "W-UNUSED-SOURCE";
  }
  return "W-UNUSED-SOURCE";  // unreachable: every enumerator is handled
}

// ---------------------------------------------------------------------------
// Views.

/// A reference station of a file, resolved: what prediction reads. Like an
/// iterator: made only by the file, valid while a HarmonicsFile sharing its
/// records lives (copies share them; providers hold a HarmonicsFile by value
/// for the duration of a task), and equal to another view exactly when both
/// are the same station of the same records.
class ReferenceView {
 public:
  [[nodiscard]] ReferenceIndex index() const noexcept { return index_; }
  [[nodiscard]] const StationInfo& info() const noexcept;
  /// The effective licence: the station's, else its source's.
  [[nodiscard]] const Licence& licence() const noexcept;
  [[nodiscard]] const Source& source() const noexcept;
  /// The file's dictionary, which every constant's index belongs to.
  [[nodiscard]] const ConstituentSet& constituents() const noexcept;
  /// One per constituent at most, sorted by ConstituentIndex (a sorted map,
  /// whatever the file's row order).
  [[nodiscard]] std::span<const Constant> constants() const noexcept;
  /// The constant of constituent `i`, if the station has one (binary
  /// search).
  [[nodiscard]] std::optional<HarmonicConstant> constant(
      ConstituentIndex i) const noexcept;
  /// The analysis mean above the station's MSL.
  [[nodiscard]] Length z0() const noexcept;
  [[nodiscard]] std::optional<Analysis> analysis() const noexcept;

  friend bool operator==(const ReferenceView&, const ReferenceView&) = default;

 private:
  friend class HarmonicsFile;
  friend struct detail::HarmonicsAccess;
  ReferenceView(const detail::HarmonicsData& data,
                ReferenceIndex index) noexcept
      : data_{&data}, index_{index} {}

  const detail::HarmonicsData* data_;  // non-owning
  ReferenceIndex index_;
};

/// A subordinate station of a file, resolved. Same lifetime and equality as
/// ReferenceView.
class SubordinateView {
 public:
  [[nodiscard]] SubordinateIndex index() const noexcept { return index_; }
  [[nodiscard]] const StationInfo& info() const noexcept;
  [[nodiscard]] const Licence& licence() const noexcept;
  [[nodiscard]] const Source& source() const noexcept;
  /// Always a reference station: chains cannot be expressed.
  [[nodiscard]] ReferenceView reference() const noexcept;
  [[nodiscard]] const SubordinateOffsets& offsets() const noexcept;
  /// d_R(D): the height of the offsets' datum above the reference's MSL,
  /// published or computed (0 for MSL). Present by construction: make()
  /// refuses a reference that lacks it.
  [[nodiscard]] Length reference_datum_height() const noexcept;

  friend bool operator==(const SubordinateView&,
                         const SubordinateView&) = default;

 private:
  friend class HarmonicsFile;
  friend struct detail::HarmonicsAccess;
  SubordinateView(const detail::HarmonicsData& data,
                  SubordinateIndex index) noexcept
      : data_{&data}, index_{index} {}

  const detail::HarmonicsData* data_;  // non-owning
  SubordinateIndex index_;
};

// ---------------------------------------------------------------------------
// The file.

struct HarmonicsBuild;

/// A resolved harmonics file. Immutable, so copies share one set of records
/// (a copy is cheap); a move is a copy, so a moved-from file stays whole.
///
/// Equality is of content, and independent of the input's order of sources,
/// stations, dictionary entries and rows: sources, references and
/// subordinates are stored sorted by id, the dictionary in canonical order
/// (docs/harmonics-json.md section 3), and each station's constants by
/// constituent index. It does depend on the order of flags,
/// provenance.also_in and provenance.dropped_constituents, which are kept as
/// written, and on the exact text of every string (licences included).
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): no moves.
class HarmonicsFile {
 public:
  /// Checks `input` and builds the file: the first of validate(input)'s
  /// errors when there are any, else the file and validate's warnings.
  [[nodiscard]] static std::expected<HarmonicsBuild, HarmonicsError> make(
      HarmonicsInput input);

  // Copy operations only: a move copies the shared records' pointer, so no
  // file is ever empty.
  HarmonicsFile(const HarmonicsFile&) = default;
  HarmonicsFile& operator=(const HarmonicsFile&) = default;
  ~HarmonicsFile() = default;

  [[nodiscard]] const FileHeader& header() const& noexcept;
  const FileHeader& header() const&& = delete;
  /// Sorted by id.
  [[nodiscard]] std::span<const Source> sources() const& noexcept;
  std::span<const Source> sources() const&& = delete;
  [[nodiscard]] const Source& source(SourceIndex i) const& noexcept;
  const Source& source(SourceIndex) const&& = delete;
  [[nodiscard]] const ConstituentSet& constituents() const& noexcept;
  const ConstituentSet& constituents() const&& = delete;

  [[nodiscard]] std::size_t reference_count() const noexcept;
  [[nodiscard]] std::size_t subordinate_count() const noexcept;

  /// Every reference, in id order (a view; no allocation).
  [[nodiscard]] auto references() const& {
    return std::views::iota(std::size_t{0}, reference_count()) |
           std::views::transform([data = data_.get()](std::size_t i) {
             return ReferenceView{*data, ReferenceIndex{i}};
           });
  }
  void references() const&& = delete;  // would dangle
  /// Every subordinate, in id order (a view; no allocation).
  [[nodiscard]] auto subordinates() const& {
    return std::views::iota(std::size_t{0}, subordinate_count()) |
           std::views::transform([data = data_.get()](std::size_t i) {
             return SubordinateView{*data, SubordinateIndex{i}};
           });
  }
  void subordinates() const&& = delete;  // would dangle

  /// An index of another file may be out of range: asserted.
  [[nodiscard]] ReferenceView reference(ReferenceIndex i) const& noexcept;
  ReferenceView reference(ReferenceIndex) const&& = delete;
  [[nodiscard]] SubordinateView subordinate(SubordinateIndex i) const& noexcept;
  SubordinateView subordinate(SubordinateIndex) const&& = delete;

  /// The station with exactly this id.
  [[nodiscard]] std::optional<StationIndex> find(
      std::string_view id) const noexcept;
  [[nodiscard]] const StationInfo& info(StationIndex i) const&;
  const StationInfo& info(StationIndex) const&& = delete;
  /// The station's licence, else its source's.
  [[nodiscard]] const Licence& effective_licence(StationIndex i) const&;
  const Licence& effective_licence(StationIndex) const&& = delete;

  friend bool operator==(const HarmonicsFile& a, const HarmonicsFile& b);

 private:
  friend struct detail::HarmonicsAccess;
  explicit HarmonicsFile(std::shared_ptr<const detail::HarmonicsData> data)
      : data_{std::move(data)} {}

  std::shared_ptr<const detail::HarmonicsData> data_;  // never null
};

/// A built file and the warnings it was accepted with.
struct HarmonicsBuild {
  HarmonicsFile file;
  std::vector<HarmonicsWarning> warnings;
};

/// Every error HarmonicsFile::make checks for, in its pinned order
/// (docs/harmonics-engine.md 2.7), and the warnings. A check that depends on
/// a failed one is skipped (listed there).
struct HarmonicsReport {
  std::vector<HarmonicsError> errors;
  std::vector<HarmonicsWarning> warnings;
  [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

[[nodiscard]] HarmonicsReport validate(const HarmonicsInput& input);

/// The year table of the file's dictionary: what a prediction of any of the
/// file's references indexes with its constants.
[[nodiscard]] std::expected<YearArguments, YearOutOfRange> year_arguments(
    const HarmonicsFile& file, std::chrono::year y);
/// The same table, reached from one of the file's stations.
[[nodiscard]] std::expected<YearArguments, YearOutOfRange> year_arguments(
    const ReferenceView& station, std::chrono::year y);

}  // namespace mov::core::tide
