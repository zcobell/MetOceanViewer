// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The station catalog: one provider's stations and what each can serve, as
// the app's station layers and the CLI's selections use them, and its JSON
// file format `metoceanviewer-stations` 1.0 (docs/station-catalog.md, the
// spec; docs/providers-design.md sections 4.6 and 7).

#pragma once

#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/provider/capabilities.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"

namespace mov::io {

/// The `format` member of every catalog file.
inline constexpr std::string_view catalog_format = "metoceanviewer-stations";

/// The version this reader and writer implement: "1.0". A reader takes any
/// 1.x; a newer minor is read with `minor_newer` and its unknown members and
/// tokens ignored (docs/station-catalog.md section 9).
inline constexpr unsigned catalog_major = 1;
inline constexpr unsigned catalog_minor = 0;

/// The providers a catalog can hold: those with Capabilities.
template <class P>
concept CatalogProvider = std::same_as<P, core::provider::Coops> or
                          std::same_as<P, core::provider::Usgs> or
                          std::same_as<P, core::provider::Ndbc>;

/// One document a catalog was built from.
struct CatalogSource {
  /// Where it was fetched, as text (no format is imposed).
  core::StationText url;
  /// When; the file holds whole seconds.
  core::Time retrieved;
  friend bool operator==(const CatalogSource&, const CatalogSource&) = default;
};

/// What a catalog says about itself.
struct CatalogHeader {
  /// When it was built; the file holds whole seconds. The startup choice
  /// and the refresh rules below compare it.
  core::Time generated;
  std::vector<CatalogSource> sources;
  friend bool operator==(const CatalogHeader&, const CatalogHeader&) = default;
};

/// One station of a catalog and what it can serve.
template <CatalogProvider P>
struct CatalogEntry {
  core::GaugeStation<P> station;
  core::Capabilities<P> caps;
  friend bool operator==(const CatalogEntry&, const CatalogEntry&) = default;
};

/// Two entries with one id (the smallest such id).
template <CatalogProvider P>
struct DuplicateStation {
  core::StationId<P> id;
  friend bool operator==(const DuplicateStation&,
                         const DuplicateStation&) = default;
};

/// One provider's stations: sorted by id, each id once. Built only by make,
/// so a catalog with a repeated station cannot exist, and found by binary
/// search (loaded once into this table, never re-parsed per lookup: B25).
template <CatalogProvider P>
class Catalog {
 public:
  using Entry = CatalogEntry<P>;

  /// Sorts the entries by id; DuplicateStation for the smallest repeated id.
  [[nodiscard]] static std::expected<Catalog, DuplicateStation<P>> make(
      CatalogHeader header, std::vector<Entry> entries);

  [[nodiscard]] const CatalogHeader& header() const& noexcept {
    return header_;
  }
  const CatalogHeader& header() const&& = delete;
  [[nodiscard]] std::span<const Entry> entries() const& noexcept {
    return entries_;
  }
  std::span<const Entry> entries() const&& = delete;
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
  [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

  /// The entry of the station, or nullptr. Points into the catalog.
  [[nodiscard]] const Entry* find(const core::StationId<P>& id) const& noexcept;
  const Entry* find(const core::StationId<P>& id) const&& = delete;

  friend bool operator==(const Catalog&, const Catalog&) = default;

 private:
  Catalog(CatalogHeader header, std::vector<Entry> sorted)
      : header_{std::move(header)}, entries_{std::move(sorted)} {}

  CatalogHeader header_;
  std::vector<Entry> entries_;
};

using CoopsCatalog = Catalog<core::provider::Coops>;
using UsgsCatalog = Catalog<core::provider::Usgs>;
using NdbcCatalog = Catalog<core::provider::Ndbc>;

/// A catalog of whichever provider its file names.
using StationCatalog = std::variant<CoopsCatalog, UsgsCatalog, NdbcCatalog>;

/// The provider of the catalog.
[[nodiscard]] core::DataSource source_of(const StationCatalog& catalog);

// ---- reading
// -----------------------------------------------------------------

/// Reads a catalog file's text (docs/station-catalog.md; the order of checks
/// is its section 8.3, so a file gives the same error every time).
///
/// - Syntax and limits: as every JSON reader of io (detail::json::parse): a
///   leading byte order mark is skipped; `empty_input`, `not_json`,
///   `too_large` (nesting deeper than 16, more than ctx.limits.max_text_bytes,
///   more stations than ctx.limits.max_elements / 16, more than 4096 series
///   at a station, 256 sources or 64 tokens in a set); a repeated member is
///   `bad_member`.
/// - `format` absent or another text: FormatError `not_this_format`.
///   `version` not "<major>.<minor>": `bad_version`; a major other than 1:
///   `unsupported_version` (subject: the version).
/// - Then the first value the format refuses, in document order, as a
///   ParseError at that value: `bad_member` (a member missing or of the wrong
///   JSON type; a provider, id, token or text the format does not allow; a
///   set that repeats a token; a station without its provider's block, or
///   with another provider's); `bad_date` (a date or timestamp that is not
///   exactly `YYYY-MM-DD` or `YYYY-MM-DDThh:mm:ssZ`, or not a real date);
///   `out_of_range` (a position Location::make refuses, a nonzero MSL
///   height, a range whose last day precedes its first). In a file of this
///   minor or an older one an unknown member or token is `bad_member` too;
///   in a newer minor each is ignored and counted (`newer_content_ignored`,
///   after `minor_newer`). Rules over a station's series (SeriesSet::make)
///   are FormatError `inconsistent_metadata` (subject: the station id).
/// - Two stations with one id: `duplicate_station_id` (subject: the id).
/// - `generated` more than a day after `now`: `future_dated` (subject: its
///   text). A clock running behind must not reject a fresh catalog.
///
/// Numbers come back as the writer prints them: positions rounded to 1e-6
/// degrees (a longitude of -180 becomes 180), heights to 1e-4 m, so
/// parse(format(c)) == c for every c this function returns. ctx.stop is
/// polled every 4096 JSON events (Cancelled).
[[nodiscard]] std::expected<Read<StationCatalog>, Error> parse_station_catalog(
    std::string_view text, core::Time now, const ReadContext& ctx);

/// parse_station_catalog on a file (read_text_file first). Blocking I/O:
/// call it from a worker.
[[nodiscard]] std::expected<Read<StationCatalog>, Error> read_station_catalog(
    const std::filesystem::path& path, core::Time now, const ReadContext& ctx);

/// The catalog of P, or FormatError `wrong_provider` (subject: the provider
/// token the file names): an app-data cache holding another provider's
/// catalog is invalid (docs/providers-design.md section 7).
template <CatalogProvider P>
[[nodiscard]] std::expected<Read<Catalog<P>>, Error> catalog_of(
    Read<StationCatalog> read) {
  auto* catalog = std::get_if<Catalog<P>>(&read.value);
  if (catalog == nullptr) {
    return std::unexpected{Error{FormatError{
        .code = FormatErrc::wrong_provider,
        .subject = std::string{core::to_token(source_of(read.value))}}}};
  }
  return Read<Catalog<P>>{.value = std::move(*catalog),
                          .warnings = std::move(read.warnings)};
}

// ---- writing
// -----------------------------------------------------------------

/// The catalog as its canonical text (docs/station-catalog.md section 7):
/// members in a fixed order, one station per line sorted by id, sets in
/// enumerator order, absent facts absent, numbers rounded (positions to 1e-6
/// degrees, heights to 1e-4 m) and printed without trailing zeros, times as
/// whole seconds (floored). The same catalog always gives the same bytes.
///
/// What the types allow but the format cannot hold is refused before any
/// text is made: a date or time outside the years 0000-9999
/// (`time_out_of_range`), a series' parameter name that is not UTF-8
/// (`bad_encoding`), a unit whose text does not parse back to the same unit
/// (`noncanonical_unit`); the subject is the station id (the header's:
/// `generated` or `sources`).
[[nodiscard]] std::expected<std::string, FormatError> format_station_catalog(
    const StationCatalog& catalog);

/// format_station_catalog, written atomically (write_file_atomic): the file
/// is the old one or the whole new one.
[[nodiscard]] std::expected<void, Error> write_station_catalog(
    const std::filesystem::path& path, const StationCatalog& catalog);

// ---- loading and refreshing (docs/providers-design.md section 7)
// -------------

/// A catalog older than this is refreshed in the background (at most once
/// per launch: the app's rule, not this one).
inline constexpr std::chrono::days catalog_max_age{30};

/// How far ahead of the reader's clock `generated` may be (clock skew).
inline constexpr std::chrono::days catalog_clock_tolerance{1};

/// Whether the catalog is due for a refresh: generated more than
/// catalog_max_age before `now`.
[[nodiscard]] bool is_stale(const CatalogHeader& header,
                            core::Time now) noexcept;

/// Where the catalog in use came from.
enum class CatalogOrigin : std::uint8_t {
  snapshot,  // compiled into the program
  cache,     // refreshed earlier, in the app-data directory
};

template <CatalogProvider P>
struct Chosen {
  Catalog<P> catalog;
  CatalogOrigin origin;
  friend bool operator==(const Chosen&, const Chosen&) = default;
};

/// The catalog to start with: the cache when it was generated strictly
/// later than the snapshot, else the snapshot. `cache` is nullopt when there
/// is none or it did not read (unparsable, future-dated, another provider:
/// the caller reports that once and passes nullopt).
template <CatalogProvider P>
[[nodiscard]] Chosen<P> choose_catalog(
    Catalog<P> snapshot, std::type_identity_t<std::optional<Catalog<P>>> cache);

/// A refreshed catalog with fewer than half the stations of the one in use
/// (an upstream outage must not empty the map).
struct TooFewStations {
  std::size_t current;
  std::size_t refreshed;
  friend constexpr bool operator==(TooFewStations, TooFewStations) = default;
};

/// The refreshed catalog when it may replace the current one: it has at
/// least half as many stations (rounded up). Valid it already is: a
/// Catalog cannot be anything else.
template <CatalogProvider P>
[[nodiscard]] std::expected<Catalog<P>, TooFewStations> accept_refresh(
    const Catalog<P>& current, Catalog<P> refreshed);

}  // namespace mov::io
