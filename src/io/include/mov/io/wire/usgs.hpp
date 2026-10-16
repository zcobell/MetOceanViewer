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
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"
#include "mov/core/windows.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/io/wire/service.hpp"

/// The USGS Water Data OGC API on the wire (docs/providers-design.md
/// section 4.9): the requests of a resolved core::usgs request and the
/// readers of the responses. Bodies are GeoJSON FeatureCollections
/// (`f=json`; CSV has no next links). Continuous times are UTC instants,
/// daily values local calendar days (decision 34); units are what USGS
/// delivers (decision 13). A logical series is fetched epoch by epoch
/// (decision 37): every data target and reader takes one core::usgs::Epoch.
/// The facts are pinned by the recordings in tests/fixtures/io/usgs/
/// (provenance.txt). Every text a warning or error carries from a body
/// (a qualifier, a unit, a link, a message) is raw server text, which the
/// edge renders as plain text.
namespace mov::io::wire::usgs {

// ---- endpoints
// ------------------------------------------------------------------

/// The OGC API collections the app reads.
enum class Endpoint : std::uint8_t {
  continuous,         ///< instantaneous values (statistic 00011)
  daily,              ///< daily values (any other statistic)
  combined_metadata,  ///< a series and its monitoring location, per row
};

/// The endpoint of a core collection's values.
[[nodiscard]] constexpr Endpoint endpoint_of(
    core::usgs::Collection collection) noexcept {
  switch (collection) {
    case core::usgs::Collection::continuous:
      return Endpoint::continuous;
    case core::usgs::Collection::daily:
      break;
  }
  return Endpoint::daily;
}

/// `/ogcapi/v1/collections/<collection>/items`: the path every request of
/// the endpoint goes to, and the path its `next` links are pinned to.
[[nodiscard]] constexpr std::string_view items_path(
    Endpoint endpoint) noexcept {
  switch (endpoint) {
    case Endpoint::continuous:
      return "/ogcapi/v1/collections/continuous/items";
    case Endpoint::daily:
      return "/ogcapi/v1/collections/daily/items";
    case Endpoint::combined_metadata:
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

/// A window that cannot be asked for, and why.
struct TargetError {
  TargetErrc reason;
  std::variant<core::TimeRange, core::DayRange> window;
  friend bool operator==(const TargetError&, const TargetError&) = default;
};

/// Resolution (stage 1, section 5.2): the site's series that can answer the
/// request, from `combined-metadata`: `monitoring_location_id`, then by
/// parameter `parameter_code` and `statistic_id` (00011 for continuous), or
/// by id `id`; then `properties` (id, parameter_code, parameter_name,
/// statistic_id, primary, unit_of_measure, begin, end,
/// sublocation_identifier, web_description), `skipGeometry=true`, `limit`,
/// `f=json`. Every epoch and sublocation of the parameter comes back;
/// core::usgs::resolve groups them. The sublocation is not filtered on the
/// server: the rows of every sublocation say whether the choice is
/// ambiguous.
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

/// One epoch's instantaneous values over `window` (a window of the epoch's
/// range), from `continuous`: `time_series_id` (the epoch's id),
/// `datetime`, `properties=time,value,approval_status,qualifier`,
/// `skipGeometry=true`, `limit`, `f=json`. The API's interval is closed and
/// its times whole seconds, so [b, e) is sent as the seconds it holds: the
/// first whole second at or after b, the last before e
/// (`yyyy-mm-ddThh:mm:ssZ/...`): adjacent windows share no sample. No
/// `sortby`: with it the API serves only the first page.
[[nodiscard]] std::expected<Composed, TargetError> data_target(
    const core::usgs::Epoch<core::usgs::Continuous>& epoch,
    core::TimeRange window);

/// One epoch's daily values over `days`, from `daily`: as above, with
/// `datetime` the first and the last day (`yyyy-mm-dd/...`; the API's
/// interval is closed, so the half-open range ends a day earlier: v4 bug L6
/// asked for a day more).
[[nodiscard]] std::expected<Composed, TargetError> data_target(
    const core::usgs::Epoch<core::usgs::Daily>& epoch, core::DayRange days);

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
/// service's refusal (classify_error) when it is markup or its first member
/// is `error` or `code`, else a Payload (JSON that does not parse included:
/// its parser locates the error). Classification by the first member is a
/// choice: an OGC error that put `type` first would be read as a page and
/// refused there (bad_member). A leading byte order mark is skipped. ctx's
/// limits and stop token apply.
[[nodiscard]] Classified classify_body(std::string_view body,
                                       std::string_view content_type,
                                       const ReadContext& ctx);

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
/// Markup is `unexpected_content` with the Content-Type. Any other body
/// (one that does not parse within ctx included) is `unknown` with its own
/// text. The text is the message or description.
[[nodiscard]] ServiceError classify_error(std::string_view body,
                                          std::string_view content_type,
                                          const ReadContext& ctx);

/// The first and last time on a page of instantaneous values: first <=
/// last.
class TimeExtent {
 public:
  [[nodiscard]] static std::optional<TimeExtent> make(core::Time first,
                                                      core::Time last) noexcept;
  [[nodiscard]] core::Time first() const noexcept { return first_; }
  [[nodiscard]] core::Time last() const noexcept { return last_; }
  friend bool operator==(const TimeExtent&, const TimeExtent&) = default;

 private:
  TimeExtent(core::Time first, core::Time last) noexcept
      : first_{first}, last_{last} {}
  core::Time first_;
  core::Time last_;
};

/// The page has no next page.
struct NoNext {
  friend bool operator==(NoNext, NoNext) = default;
};
/// A `cursor` link (or one with neither `cursor` nor `offset`): the next
/// page.
struct Next {
  Verbatim link;
  friend bool operator==(const Next&, const Next&) = default;
};
/// The link the API adds to every full last page of a cursor chain:
/// `offset` and `limit` both the page's number of features, no `cursor`.
/// From a chain's later page it re-reads the window from that offset of
/// its start (recorded 2026-10-15: a cycle), so P7 follows it only from a
/// chain's first page (where it reads the rest of an offset-paged result,
/// or an empty page) and otherwise ends the chain.
struct OffsetEcho {
  Verbatim link;
  friend bool operator==(const OffsetEcho&, const OffsetEcho&) = default;
};
using NextPage = std::variant<NoNext, Next, OffsetEcho>;

/// What a page says about paging, besides its rows: how many features it
/// had (rows skipped included) and its next page. Every link is checked by
/// Verbatim::make against the page's endpoint first (FormatError
/// refused_link); then a link with `offset` and no `cursor` is an
/// OffsetEcho only when its `offset` and `limit` both equal the features,
/// and any other such link is FormatError unexpected_link. The fetch
/// layer's PageGuard bounds the rest.
struct Paging {
  std::size_t features = 0;
  NextPage next = NoNext{};
  friend bool operator==(const Paging&, const Paging&) = default;
};

/// A page of one epoch's instantaneous values: the epoch's series (its
/// quantity, label, unit and datum) in strictly increasing time, and the
/// paging. The API serves them in time order, page after page.
struct ContinuousPage {
  core::TimeSeries values;
  Paging paging;

  /// The first and last time (nullopt when the page has none).
  [[nodiscard]] std::optional<TimeExtent> extent() const;

  friend bool operator==(const ContinuousPage&,
                         const ContinuousPage&) = default;
};

/// A page of one epoch's daily values, by day (strictly increasing). The
/// API serves them in no time order (by feature id), on one page and across
/// pages, so a page has no extent and the pages of a chain are merged by
/// day.
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
/// page of `epoch`'s values: the one entry point, so no body is read before
/// it is classified.
///
/// Each feature's `properties`: `time` an RFC 3339 instant
/// (core::parse_rfc3339); `value` a JSON string read by
/// detail::parse_double, `null` or "" a Missing sample; `approval_status` a
/// string or null; `qualifier` null, a string or an array of at most 256
/// strings (recorded: `["ESTIMATED"]`). The series' metadata is the epoch's:
/// quantity core::usgs::quantity_of(parameter), label the parameter name,
/// unit the epoch's unit, datum the registry's (registry_quantity).
/// Warnings: `provisional_samples` (subject `usgs:approval_status`) for
/// "Provisional" samples; `qualified_samples` per qualifier (subject: the
/// qualifier, at most 16 distinct, the rest under `...`; P7 sums the pages'
/// warnings by code and subject without applying the cap again); times out
/// of order put in order (times_reordered, duplicate_times_dropped,
/// conflicting_duplicate_times, subject `usgs:continuous`).
/// Errors (io::Error): ParseError `not_json`; `too_large` (nesting past
/// max_json_depth, a body over ctx.limits.max_text_bytes, more features
/// than ctx.limits.max_elements, more than 64 links, a qualifier list over
/// 256); `bad_member` (no `features` array, a feature or its `properties`
/// not an object, a member missing, repeated or of the wrong JSON type, two
/// `next` links); `count_mismatch` (`numberReturned` is not the number of
/// features); `bad_date`; `bad_number` or `out_of_range` for a value;
/// `empty_input`; FormatError `refused_link` (subject: Verbatim's reason and
/// the link) or `unexpected_link` (an offset link that is no echo);
/// Cancelled (ctx.stop).
[[nodiscard]] std::expected<Reply<ContinuousPage>, Error> read_continuous(
    std::string_view body, std::string_view content_type,
    const core::usgs::Epoch<core::usgs::Continuous>& epoch,
    const ReadContext& ctx);

/// As read_continuous, for a `daily` body: `time` is a date
/// ("yyyy-mm-dd", the site's local day; core::parse_date), the values a
/// core::DailySeries of the epoch's statistic. Days are put in order without
/// a warning (the API's order is not a time order); a repeated day keeps
/// the first and warns (duplicate_times_dropped, conflicting_duplicate_times,
/// subject `usgs:daily`).
[[nodiscard]] std::expected<Reply<DailyPage>, Error> read_daily(
    std::string_view body, std::string_view content_type,
    const core::usgs::Epoch<core::usgs::Daily>& epoch, const ReadContext& ctx);

/// Classifies a `combined-metadata` body and reads its rows as series:
/// `id` a TimeSeriesId, `parameter_code`, `statistic_id`, `parameter_name`
/// (null: ""), `primary` ("Primary": true; null or any other text: false),
/// `unit_of_measure` (unit_of_measure()), `sublocation_identifier` (null:
/// none), `web_description` (null: ""), `begin` and `end` (RFC 3339 or
/// null) as the UTC days they fall on. Validity is advisory: dates that do
/// not parse, or an end before the begin, keep the row with an unknown
/// validity and count `validity_ignored` (subject `dates`). A row that
/// cannot be a series is left out and counted (`series_skipped`, subject:
/// `id`, `parameter`, `statistic`, `sublocation`, `member` for a member of
/// the wrong JSON type, and for the catalog `station`, `name`, `position`):
/// the field-measurement rows (UUID ids, no statistic) when the request did
/// not filter them out. Errors as read_continuous.
[[nodiscard]] std::expected<Reply<SeriesPage>, Error> read_series(
    std::string_view body, std::string_view content_type,
    const ReadContext& ctx);

/// As read_series, for the catalog (catalog_target): each row also has
/// `monitoring_location_id` (a StationId<Usgs>), `monitoring_location_name`
/// (StationText; null: "") and a GeoJSON Point geometry `[lon, lat]` (a
/// height or further coordinates ignored, at most 16; null geometry: the
/// row is skipped, `position`).
[[nodiscard]] std::expected<Reply<CatalogPage>, Error> read_catalog(
    std::string_view body, std::string_view content_type,
    const ReadContext& ctx);

}  // namespace mov::io::wire::usgs
