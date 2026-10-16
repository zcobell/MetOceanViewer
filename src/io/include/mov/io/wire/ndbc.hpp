// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/overloaded.hpp"
#include "mov/core/provider/capabilities.hpp"
#include "mov/core/provider/ndbc.hpp"
#include "mov/core/station.hpp"
#include "mov/io/error.hpp"
#include "mov/io/part.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/io/wire/service.hpp"

/// The National Data Buoy Center on the wire (docs/providers-design.md
/// sections 1.3, 4.4, 4.5): the standard meteorological files of a station
/// and the active station list. Plain files, no API and no key; the facts
/// are pinned by the recordings in tests/fixtures/io/ndbc/ (provenance.txt).
namespace mov::io::wire::ndbc {

/// An NDBC id: four to seven letters and digits (plan decision 38), so
/// four-character shore stations (KATP) and seven-digit DART buoys (5401000)
/// are stations like the five-character buoys.
using Station = core::StationId<core::provider::Ndbc>;

// ---- which file
// ------------------------------------------------------------------

/// A year's quality-controlled file, `historical/stdmet/{id}h{yyyy}.txt.gz`.
struct Historical {
  std::chrono::year year;
  friend constexpr bool operator==(Historical, Historical) = default;
};

/// How a finalized month is written in its file name: `letter` is NDBC's
/// month code 1-9 then a-c for October to December (believed, not verified:
/// section 1.3), `decimal` is 1-12, the fallback the plan tries for October
/// to December (fetch::ndbc::MonthlyDecimal).
enum class MonthSpelling : std::uint8_t { letter, decimal };

/// A finalized month of the current year,
/// `stdmet/{Mon}/{id}{m}{yyyy}.txt.gz`.
struct Monthly {
  std::chrono::year_month month;
  MonthSpelling spelling;
  friend constexpr bool operator==(Monthly, Monthly) = default;
};

/// The latest month, not yet finalized, `stdmet/{Mon}/{id}.txt`. Its name
/// has no year, so its rows are clipped to the month asked for
/// (fetch::ndbc::extent, io::clip) before they are merged.
struct LatestMonth {
  std::chrono::month month;
  friend constexpr bool operator==(LatestMonth, LatestMonth) = default;
};

/// `realtime2/{ID}.txt`: about the last 45 days, newest first.
struct Realtime {
  friend constexpr bool operator==(Realtime, Realtime) = default;
};

/// One standard-met file of a station.
using File = std::variant<Historical, Monthly, LatestMonth, Realtime>;

/// The kind of a file, which sets its parse's unit defaults and warning
/// subjects (section 4.4).
[[nodiscard]] constexpr core::ndbc::FileKind kind(const File& file) noexcept {
  using core::ndbc::FileKind;
  return std::visit(
      core::Overloaded{[](Historical) { return FileKind::historical; },
                       [](Monthly) { return FileKind::monthly; },
                       [](LatestMonth) { return FileKind::monthly_latest; },
                       [](Realtime) { return FileKind::realtime; }},
      file);
}

/// "Jan" ... "Dec", as NDBC names its month directories; nullopt for a month
/// that is not ok().
[[nodiscard]] std::optional<std::string_view> month_directory(
    std::chrono::month m) noexcept;

/// The file-name code of a finalized month: "1"-"9", then "a"-"c" (letter)
/// or "10"-"12" (decimal); nullopt for a month that is not ok().
[[nodiscard]] std::optional<std::string_view> month_code(
    std::chrono::month m, MonthSpelling spelling) noexcept;

// ---- targets
// ---------------------------------------------------------------------

/// Why a file cannot be named.
enum class TargetErrc : std::uint8_t {
  year_out_of_range,  // file names spell years 0000-9999 only
  month_out_of_range  // not a month 1-12
};

struct TargetError {
  TargetErrc reason;
  friend constexpr bool operator==(TargetError, TargetError) = default;
};

/// The request of a station's file, on Origin::ndbc with no query:
///   Historical   /data/historical/stdmet/{id}h{yyyy}.txt.gz
///   Monthly      /data/stdmet/{Mon}/{id}{m}{yyyy}.txt.gz
///   LatestMonth  /data/stdmet/{Mon}/{id}.txt
///   Realtime     /data/realtime2/{ID}.txt
/// The id is lower-case in the first three and upper-case in realtime2,
/// the only spelling each directory serves (the other is 404, section 1.3).
[[nodiscard]] std::expected<Composed, TargetError> file_target(
    const Station& station, const File& file);

/// /activestations.xml.
[[nodiscard]] Composed active_stations_target();

// ---- standard meteorological data
// ---------------------------------------------------

/// A standard-met body, read: its columns, or the service's refusal (an HTML
/// page where a text file was expected: ServiceError unexpected_content). A
/// missing file is HTTP 404, which the fetch layer handles before any body
/// is read.
using StdMetReply = std::variant<Read<Part>, ServiceError>;

/// The most text a standard-met body may hold, compressed or not, whatever
/// larger ctx.limits.max_text_bytes allows. The largest real files measured
/// (2026-10-16) are a year of 10-minute rows: 44013h2024 inflates to
/// 4,735,745 bytes, 41002h2019 to 4,509,358; realtime2 files are about
/// 0.3 MB. 64 MiB is more than thirteen such years.
inline constexpr std::uintmax_t max_stdmet_bytes = std::uintmax_t{64} << 20;

/// The one entry point for a body as received: inflated when it starts with
/// the gzip magic (io::gunzip; a `.txt.gz` file arrives compressed, a `.txt`
/// file or a body Qt already inflated does not), then sniffed for markup
/// (sniff_markup), then parse_stdmet. The text, inflated or not, is at most
/// min(ctx.limits.max_text_bytes, max_stdmet_bytes); more is `too_large`. A
/// gzip failure is a ParseError `bad_gzip`, its column the byte offset into
/// the body.
[[nodiscard]] std::expected<StdMetReply, Error> read_stdmet(
    std::span<const std::byte> body, std::string_view content_type,
    core::ndbc::FileKind kind, const ReadContext& ctx);

/// One standard-met file's text, read by its own header (v4 used the first
/// file's header for every year, L14; and found the minute column by
/// position, L13):
///  - Header line 1, the first non-blank line: `YY` or `YYYY` (a leading
///    `#` is dropped), `MM`, `DD`, `hh`, then `mm` when the file has minutes
///    (from 2005), then the data columns. A second line whose first token
///    starts with `#` is the units row: as many tokens, the time columns
///    `#yr mo dy hr [mn]`.
///  - Columns by token, never by position, with the old names `WD` (WDIR)
///    and `BAR` (PRES): WDIR wind_direction, WSPD wind_speed, GST wind_gust,
///    WVHT wave_height, DPD wave_period_dominant, APD wave_period_average,
///    MWD wave_direction, PRES air_pressure, ATMP air_temperature, WTMP
///    water_temperature, DEWP dew_point, VIS visibility, TIDE water_level
///    above MLLW, PTDY the generic `pressure_tendency` (three-hour change,
///    hPa). Any other token is skipped (`skipped_variable`, subject the
///    token); a column named twice is FormatError duplicate_quantity.
///  - Units from the units row, each of its column's kind (direction degT
///    or deg, speed, length, period sec, pressure, temperature), else
///    FormatError noncanonical_unit (subject "<column> <unit>"). Without a
///    units row (the files before 2007) the units of NDBC's measurement
///    descriptions: m/s, m, sec, degT, hPa, degC, TIDE ft and VIS nmi
///    ("Station visibility (nautical miles)"), whatever the file kind. A
///    units row wins (2019's historical file says VIS `mi`).
///  - Years: two digits are 19YY (files before 1999), four as written. The
///    time is UTC; without a minute column the minute is 0.
///  - Missing values, exact per column: `MM` in any column (realtime2);
///    otherwise WDIR and MWD 999, WSPD, GST and VIS 99, WVHT, DPD, APD and
///    TIDE 99 (written 99.00), ATMP, WTMP and DEWP 999, PRES 9999; PTDY has
///    none. 999.0 hPa is a real pressure and stays (v4 masked it; L12).
///    Each 9-filled value masked is counted (`legacy_sentinel_masked`,
///    subject "ndbc:<column token as written>"); anything else that is not
///    a number is a ParseError (v4 read `MM` as 0.0, L12).
///  - Lines after the header that start with `#` are comments, skipped and
///    counted (`header_line_skipped`, subject the first one).
///  - Order: a realtime2 file is written newest first. When its times
///    never increase it is reversed, rows of one time kept in file order;
///    then every file goes through normalize, so only true faults (a
///    repeated time, a row out of place) are reported, about "ndbc:<kind>"
///    (historical, monthly, monthly_latest, realtime). Of rows with one
///    time the first in the file is kept (plan decision 30.3).
/// Every recognized column is in the part, all-Missing ones included; a
/// header without data rows gives empty columns.
/// Errors: empty_input; missing_header (no time columns as above, with
/// the column at the token); wrong_field_count (a row or the units row with
/// another number of tokens than the header); bad_date (a year of other
/// than 2 or 4 digits, a month, day, hour or minute out of range);
/// bad_number, out_of_range (a value); too_large (more samples than
/// ctx.limits.max_elements, or the text over max_text_bytes); Cancelled.
[[nodiscard]] std::expected<Read<Part>, Error> parse_stdmet(
    std::string_view text, core::ndbc::FileKind kind, const ReadContext& ctx);

// ---- the active station list
// --------------------------------------------------------

/// The data feeds activestations.xml flags with "y" or "n"; nullopt where
/// the station's element has no such attribute (the TAO buoys have none).
/// A flag is the list's word only: on 2026-10-15 41002 and 42040 had met
/// "n" yet served standard met files.
struct Feeds {
  std::optional<bool> met;
  std::optional<bool> currents;
  std::optional<bool> water_quality;
  std::optional<bool> dart;
  friend constexpr bool operator==(Feeds, Feeds) = default;
};

/// One station of the list: id, name and position as listed (validity
/// unknown and ongoing: the list has no dates); owner, `pgm` (program),
/// `type` (platform) and `elev` (metres above mean sea level) as its
/// capabilities. The texts are the server's, with the references decoded
/// (`&lt;b&gt;` is `<b>`): whatever shows them renders them as plain text.
struct StationRecord {
  core::GaugeStation<core::provider::Ndbc> station;
  core::Capabilities<core::provider::Ndbc> caps;
  Feeds feeds;
  friend bool operator==(const StationRecord&, const StationRecord&) = default;
};

/// Parses activestations.xml with a strict subset scanner, not an XML
/// parser (section 4.5): an optional byte order mark and XML declaration
/// (version 1.0, encoding UTF-8), comments and white space, one `stations`
/// element (attributes ignored) holding only self-closing `station`
/// elements. Attribute values are quoted, with the five predefined entities
/// and character references; a literal tab, CR or LF in a value is refused
/// (XML would normalize it). Anything else (a DTD, CDATA, a processing
/// instruction, text, another element, nesting, a namespace, invalid UTF-8)
/// is a ParseError `corrupt_record`, so a format change fails closed.
///
/// Stations are read by attribute name; unknown attributes are ignored. A
/// station is left out, counted by reason (`station_skipped`, subject
/// `id` for a missing id or one that is not an NDBC id, `duplicate` for an
/// id listed before, `position`, `elevation`, `text` for a name, owner,
/// pgm or type that is not station text, `feeds` for a flag other than "y"
/// or "n").
/// Errors: corrupt_record, empty_input, too_large (more than
/// ctx.limits.max_elements / 16 stations, or the text over max_text_bytes),
/// Cancelled.
[[nodiscard]] std::expected<Read<std::vector<StationRecord>>, Error>
read_active_stations(std::string_view text, const ReadContext& ctx);

}  // namespace mov::io::wire::ndbc
