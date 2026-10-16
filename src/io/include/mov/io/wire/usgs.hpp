// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/daily_series.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/core/windows.hpp"
#include "mov/io/error.hpp"
#include "mov/io/part.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/io/wire/service.hpp"

/// The USGS Water Data OGC API on the wire (docs/providers-design.md
/// section 4): the requests of a core::usgs request and the readers of the
/// responses. Bodies are GeoJSON FeatureCollections (`f=json`; CSV has no
/// next links). Continuous times are UTC instants, daily values local
/// calendar days (decision 34); units are what USGS delivers (decision 13).
/// The facts are pinned by the recordings in tests/fixtures/io/usgs/
/// (provenance.txt).
namespace mov::io::wire::usgs {

// ---- collections
// --------------------------------------------------------------

/// The OGC API collections the app reads.
enum class Collection : std::uint8_t {
  continuous,         ///< instantaneous values (statistic 00011)
  daily,              ///< daily values (any other statistic)
  combined_metadata,  ///< a series and its monitoring location, per row
};

/// `/ogcapi/v1/collections/<collection>/items`: the path every request of
/// the collection goes to, and the path its `next` links are pinned to.
[[nodiscard]] constexpr std::string_view items_path(
    Collection collection) noexcept {
  switch (collection) {
    case Collection::continuous:
      return "/ogcapi/v1/collections/continuous/items";
    case Collection::daily:
      return "/ogcapi/v1/collections/daily/items";
    case Collection::combined_metadata:
      break;
  }
  return "/ogcapi/v1/collections/combined-metadata/items";
}

/// The rows a page holds at most (`limit`), the API's maximum: a request
/// for more is refused ("Limit of 50000 exceeded"). Without `limit` the
/// server sends 10.
inline constexpr std::uint32_t page_rows = 50000;

/// The parameters of the map layer (decision 33): discharge, gage height,
/// and the estuary or ocean water level over NAVD88 and NGVD29.
inline constexpr std::array<std::string_view, 4> map_parameters{
    "00060", "00065", "62620", "62619"};

// ---- targets
// -------------------------------------------------------------------

/// Why a window cannot be asked for.
enum class TargetErrc : std::uint8_t {
  no_whole_second,          ///< the window holds no whole second
  outside_four_digit_years  ///< `datetime` spells years 0000-9999 only
};

struct TargetError {
  TargetErrc reason;
  friend constexpr bool operator==(TargetError, TargetError) = default;
};

/// Resolution (stage 1, section 5.2): the site's series that can answer the
/// request, from `combined-metadata`: `monitoring_location_id`, then by
/// parameter `parameter_code` and `statistic_id` (00011 for continuous), or
/// by id `id`; then `properties` (id, parameter_code, parameter_name,
/// statistic_id, primary, unit_of_measure, begin, end), `skipGeometry=true`,
/// `limit`, `f=json`. Several rows can come back (several Primary series:
/// fetch::usgs::bind reports AmbiguousSeries).
[[nodiscard]] Composed resolve_target(const core::usgs::Continuous& request);
[[nodiscard]] Composed resolve_target(const core::usgs::Daily& request);

/// Every continuous and daily series of a site (the series picker, section
/// 6.6): as resolve_target, but by `monitoring_location_id` and
/// `data_type=Continuous values,Daily values` only (which leaves out the
/// field-measurement and peak rows the collection also holds).
[[nodiscard]] Composed site_series_target(
    const core::StationId<core::provider::Usgs>& station);

/// The station catalog (section 7): every instantaneous series of the map
/// parameters (decision 33), with its monitoring location's id, name and
/// geometry. One page held all 29,017 rows (20 MB) on 2026-10-15.
[[nodiscard]] Composed catalog_target();

/// The instantaneous values of a resolved series over `window`, from
/// `continuous`: `time_series_id`, `datetime`, `properties=time,value,
/// approval_status,qualifier`, `skipGeometry=true`, `limit`, `f=json`. The
/// API's interval is closed and its times whole seconds, so [b, e) is sent as
/// the seconds it holds: the first whole second at or after b, the last
/// before e (`yyyy-mm-ddThh:mm:ssZ/...`): adjacent windows share no sample.
/// No `sortby`: with it the API serves only the first page.
[[nodiscard]] std::expected<Composed, TargetError> data_target(
    const core::usgs::Resolved<core::usgs::Continuous>& series,
    core::TimeRange window);

/// The daily values of a resolved series over `days`, from `daily`: as
/// above, with `datetime` the first and the last day (`yyyy-mm-dd/...`; the
/// API's interval is closed, so the half-open range ends a day earlier: v4
/// bug L6 asked for a day more).
[[nodiscard]] std::expected<Composed, TargetError> data_target(
    const core::usgs::Resolved<core::usgs::Daily>& series, core::DayRange days);

// ---- units
// ---------------------------------------------------------------------

/// The unit of a `unit_of_measure`: the USGS spellings core does not know
/// ("ft^3/s" and "m^3/s" of the series metadata; "ft/sec", "m3/sec" and
/// "Deg" of the parameter codes) by a pinned alias table, everything else
/// through core::parse_unit, and `unrecognized_unit` (subject: the text) for
/// a unit outside core's families that no registry quantity uses (mg/l,
/// _FNU, pH Units, ...). nullopt for null or blank text.
[[nodiscard]] Read<std::optional<core::Unit>> unit_of_measure(
    std::optional<std::string_view> text);

// ---- bodies
// --------------------------------------------------------------------

/// What a USGS body says before its data is read: there is no NoData (an
/// empty page is a page).
using Classified = std::variant<Payload, ServiceError>;

/// What a body of an accepted exchange says before its data is read: the
/// service's refusal (classify_error) when it is markup or names an error
/// first (`{"error": ...}` or `{"code": ...}`), else a Payload (JSON that
/// does not parse included: its parser locates the error). A leading byte
/// order mark is skipped.
[[nodiscard]] Classified classify_body(std::string_view body,
                                       std::string_view content_type);

/// The refusal a body states (a 4xx body, or classify_body's). The two
/// shapes and the codes:
///  - api.data.gov's `{"error": {"code", "message"}}`: API_KEY_INVALID,
///    API_KEY_MISSING, API_KEY_DISABLED, API_KEY_UNAUTHORIZED and
///    API_KEY_UNVERIFIED are `api_key_invalid`; any other code `unknown`;
///  - OGC's `{"code", "type", "description"}`: a description saying "time
///    envelope is too large" is `range_exceeded` (a planner bug: continuous
///    windows are cut at 1000 days, the API refuses more than 1100);
///    InvalidParameterValue and InvalidQuery are `invalid_parameter`;
///    anything else (NotFound, a server error) `unknown`.
/// Markup is `unexpected_content` with the Content-Type. Any other body is
/// `unknown` with its own text. The text is the message or description.
[[nodiscard]] ServiceError classify_error(std::string_view body,
                                          std::string_view content_type);

/// The first and last time on a page of instantaneous values.
struct TimeExtent {
  core::Time first;
  core::Time last;
  friend bool operator==(const TimeExtent&, const TimeExtent&) = default;
};

/// What a page says about paging, besides its rows: how many features it
/// had (rows skipped included) and the link to the next page. A `next` link
/// is a Verbatim pinned to the page's collection; a link that carries
/// `offset` but no `cursor` is not a next page: the API adds one to every
/// full last page, and following it re-reads rows from the window's start
/// (recorded 2026-10-15, provenance.txt). The fetch layer's PageGuard
/// bounds the rest.
struct Paging {
  std::size_t features = 0;
  std::optional<Verbatim> next;
  friend bool operator==(const Paging&, const Paging&) = default;
};

/// A page of instantaneous values: one column, the series' (io/part.hpp),
/// in strictly increasing time; the extent of its times (nullopt when
/// empty); the paging. The API serves them in time order, page after page.
struct ContinuousPage {
  Part part;
  std::optional<TimeExtent> extent;
  Paging paging;
  friend bool operator==(const ContinuousPage&,
                         const ContinuousPage&) = default;
};

/// A page of daily values, by day (strictly increasing). The API serves
/// them in no time order (by feature id), on one page and across pages, so
/// a page has no extent and the pages of a chain are merged by day.
struct DailyPage {
  core::DailySeries values;
  Paging paging;
  friend bool operator==(const DailyPage&, const DailyPage&) = default;
};

/// A page of a site's series (resolution and the series picker), in the
/// order served.
struct SeriesPage {
  std::vector<core::usgs::SeriesInfo> series;
  Paging paging;
  friend bool operator==(const SeriesPage&, const SeriesPage&) = default;
};

/// One row of the catalog: a series and the monitoring location it belongs
/// to. A site has as many rows as it has series; the catalog builder groups
/// them.
struct SiteSeries {
  core::StationId<core::provider::Usgs> station;
  core::StationText name;
  core::Location location;
  core::usgs::SeriesInfo series;
  friend bool operator==(const SiteSeries&, const SiteSeries&) = default;
};

struct CatalogPage {
  std::vector<SiteSeries> rows;
  Paging paging;
  friend bool operator==(const CatalogPage&, const CatalogPage&) = default;
};

/// A body read: its page, or the service's refusal.
template <class Page>
using Reply = std::variant<Read<Page>, ServiceError>;

/// Classifies a `continuous` body (classify_body) and reads a Payload as a
/// page of `series`: the one entry point, so no body is read before it is
/// classified.
///
/// Each feature's `properties`: `time` an RFC 3339 instant (parse_rfc3339);
/// `value` a JSON string read by detail::parse_double, `null` or "" a
/// Missing sample; `approval_status` a string or null; `qualifier` null, a
/// string or an array of strings (recorded: `["ESTIMATED"]`). The column's
/// metadata is the series': quantity core::usgs::quantity_of(parameter),
/// label the parameter name, unit the series' unit, datum the registry's
/// (registry_quantity). Warnings: `provisional_samples` (subject
/// `usgs:approval_status`) for "Provisional" samples; `qualified_samples`
/// per qualifier (subject: the qualifier, at most 16 distinct, the rest
/// under `...`); times out of order put in order (times_reordered,
/// duplicate_times_dropped, conflicting_duplicate_times, subject
/// `usgs:continuous`).
/// Errors (io::Error): ParseError `not_json`; `too_large` (nesting past
/// max_json_depth, a body over ctx.limits.max_text_bytes, more features
/// than ctx.limits.max_elements, more than 64 links); `bad_member` (no
/// `features` array, a feature or its `properties` not an object, a member
/// missing, repeated or of the wrong JSON type, two `next` links);
/// `count_mismatch` (`numberReturned` is not the number of features);
/// `bad_date`; `bad_number` or `out_of_range` for a value; `empty_input`;
/// FormatError `foreign_link` (a `next` link Verbatim refuses: subject the
/// link); Cancelled (ctx.stop).
[[nodiscard]] std::expected<Reply<ContinuousPage>, Error> read_continuous(
    std::string_view body, std::string_view content_type,
    const core::usgs::Resolved<core::usgs::Continuous>& series,
    const ReadContext& ctx);

/// As read_continuous, for a `daily` body: `time` is a date
/// ("yyyy-mm-dd", the site's local day), the values a core::DailySeries of
/// the series' statistic. Days are put in order without a warning (the API's
/// order is not a time order); a repeated day keeps the first and warns
/// (duplicate_times_dropped, conflicting_duplicate_times, subject
/// `usgs:daily`).
[[nodiscard]] std::expected<Reply<DailyPage>, Error> read_daily(
    std::string_view body, std::string_view content_type,
    const core::usgs::Resolved<core::usgs::Daily>& series,
    const ReadContext& ctx);

/// Classifies a `combined-metadata` body and reads its rows as series:
/// `id` a TimeSeriesId, `parameter_code`, `statistic_id`, `parameter_name`
/// (null: ""), `primary` ("Primary": true; null or any other text: false),
/// `unit_of_measure` (unit_of_measure()), `begin` and `end` (RFC 3339 or
/// null) as the UTC days they fall on (null: unknown, ongoing). A row that
/// cannot be a series is left out and counted (`series_skipped`, subject:
/// `id`, `parameter`, `statistic`, `dates`, `member` for a member of the
/// wrong JSON type, `position`, `station`, `name`): the field-measurement
/// rows (UUID ids, no statistic) when the request did not filter them out.
/// Errors as read_continuous.
[[nodiscard]] std::expected<Reply<SeriesPage>, Error> read_series(
    std::string_view body, std::string_view content_type,
    const ReadContext& ctx);

/// As read_series, for the catalog (catalog_target): each row also has
/// `monitoring_location_id` (a StationId<Usgs>), `monitoring_location_name`
/// (StationText; null: "") and a GeoJSON Point geometry `[lon, lat]` (a
/// Location; null geometry: the row is skipped, `position`).
[[nodiscard]] std::expected<Reply<CatalogPage>, Error> read_catalog(
    std::string_view body, std::string_view content_type,
    const ReadContext& ctx);

}  // namespace mov::io::wire::usgs
