// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "mov/core/geo.hpp"
#include "mov/core/station.hpp"
#include "mov/core/tide/constituent_set.hpp"
#include "mov/core/tide/datums.hpp"
#include "mov/core/tide/source.hpp"
#include "mov/core/tide/station_info.hpp"
#include "mov/core/units.hpp"
#include "mov/core/version.hpp"

// The decoded but unresolved content of a harmonics file: what the io reader
// (WP H5) fills after its schema checks, and what HarmonicsFile::make
// resolves (docs/harmonics-engine.md 6.3, steps 5-7).
//
// The split of the checks: a value the schema constrains is checked by the
// reader as it builds the value types below (ConstituentName, Definition,
// SourceToken, Location, StationText, HarmonicConstant, SubordinateOffsets,
// DatumHeights; their errors are E-SCHEMA). What only the whole file can
// tell, or what docs/harmonics-json.md 10.1 names a code of its own for
// (licences, dates, uniqueness, references, speeds, datum conflicts), stays
// raw here and is HarmonicsFile::make's to check, in the order of
// docs/harmonics-json.md 10.4. Every vector is in document order, and errors
// and warnings name positions in these vectors.

namespace mov::core::tide {

/// `created`: YYYY-MM-DDThh:mm:ssZ, its fields as the digits say, not yet
/// checked as a calendar date and a time of day (E-DATE).
struct TimestampInput {
  std::chrono::year_month_day date;
  int hour;
  int minute;
  int second;
};

/// The `generator` object.
struct Generator {
  std::string name;
  std::string version;
  std::optional<std::string> command;
  friend bool operator==(const Generator&, const Generator&) = default;
};

/// The top-level members other than `sources`, `constituents` and
/// `stations`. `format` and `conventions` are fixed (the reader checks
/// them), so they are not here. The version's major is the reader's to check
/// (E-MAJOR).
struct HeaderInput {
  VersionNumber version;
  TimestampInput created;
  Generator generator;
  std::optional<std::string> title;
  std::optional<std::string> description;
  std::string licence_summary;
};

/// One element of `sources`. `licence` is parsed by make (E-LICENCE) and
/// `retrieved` checked as a calendar date (E-DATE).
struct SourceInput {
  SourceToken id;
  StationText name;
  std::optional<std::string> url;
  std::string licence;
  std::string attribution;
  std::chrono::year_month_day retrieved;
  std::optional<std::string> version;
  std::optional<std::string> sha256;
  std::optional<std::string> notes;
};

/// One element of `constituents`: the entry and its checksum speed
/// (E-SPEED-MISMATCH).
struct ConstituentInput {
  DictionaryEntry entry;
  double speed;
};

/// `record`: each date checked by make (E-DATE).
struct RecordInput {
  std::optional<std::chrono::year_month_day> start;
  std::optional<std::chrono::year_month_day> end;
  std::optional<double> years;  // finite and > 0, else E-SCHEMA
};

/// `computed_datums`: the period's dates are checked by make (E-DATE) and
/// its length (E-DATUM-CONFLICT).
struct ComputedDatumsInput {
  std::chrono::year_month_day start;
  std::chrono::year_month_day end;
  DatumHeights heights;
};

/// The members every station has (docs/harmonics-json.md 8.1). `id`'s
/// namespace must name a source (E-UNKNOWN-SOURCE); `licence` is parsed by
/// make (E-LICENCE).
struct StationInput {
  StationId<provider::Harmonics> id;
  StationText name;
  Location location;
  std::optional<std::string> licence;
  std::optional<std::string> country;
  std::optional<std::string> time_zone;
  DatumHeights datums;
  std::optional<std::string> datum_epoch;
  std::optional<RecordInput> record;
  std::vector<std::string> flags;
  Provenance provenance;
};

/// One row: the constituent by its exact name (E-UNKNOWN-CONSTITUENT,
/// E-DUPLICATE-ROW) and its checked constant.
struct ConstantInput {
  ConstituentName constituent;
  HarmonicConstant value;
};

/// A `kind: "reference"` station. `z0` must be finite (else E-SCHEMA).
struct ReferenceInput {
  StationInput station;
  std::vector<ConstantInput> constants;
  Length z0;
  std::optional<Analysis> analysis;
  std::optional<ComputedDatumsInput> computed_datums;
};

/// A `kind: "subordinate"` station: `reference` must be the id of a
/// reference station of the file (E-REFERENCE) that knows the offsets'
/// datum (E-OFFSET-DATUM).
struct SubordinateInput {
  StationInput station;
  StationId<provider::Harmonics> reference;
  SubordinateOffsets offsets;
};

using StationKindInput = std::variant<ReferenceInput, SubordinateInput>;

/// A whole file, decoded.
struct HarmonicsInput {
  HeaderInput header;
  std::vector<SourceInput> sources;
  std::vector<ConstituentInput> constituents;
  std::vector<StationKindInput> stations;
};

}  // namespace mov::core::tide
