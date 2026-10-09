// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// What the three station netCDF readers (v5, foreign CF, legacy v4) share: the
// error and text helpers, the CRS of a grid mapping, the positions of the
// stations, the datum spellings, and the rule for series whose times are not
// strictly increasing. Private to src/io/.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "model_netcdf.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/detail/ascii.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"
#include "mov/io/detail/table_error.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/projection.hpp"
#include "mov/io/read.hpp"
#include "mov/io/warning.hpp"
#include "station_netcdf_format.hpp"
#include "station_netcdf_reader.hpp"

namespace mov::io::detail::station_nc {

namespace sn = ::mov::io::detail::station_nc;

std::unexpected<Error> invalid(FormatErrc code, std::string subject,
                               std::optional<std::size_t> station,
                               std::optional<std::size_t> index) {
  return fail(format_error(code, std::move(subject), station, index));
}

std::string subject_of(std::string_view text) {
  return std::string{truncate_utf8(text, ParseError::max_context_bytes)};
}

/// A text attribute of `on`, cut at its first NUL; nullopt when absent or not
/// text.
std::expected<std::optional<std::string>, Error> text_of(const nc::File& file,
                                                         nc::AttTarget on,
                                                         nc::NcNameRef att) {
  return optional_text(file, on, att)
      .transform([](std::optional<std::string> text) {
        return text.transform(
            [](const std::string& t) { return std::string{cut_at_nul(t)}; });
      });
}

std::expected<std::optional<std::vector<std::int64_t>>, Error> int_att(
    const nc::File& file, nc::AttTarget on, nc::NcNameRef att,
    std::string_view object) {
  using Result = std::expected<std::optional<std::vector<std::int64_t>>, Error>;
  const auto type = file.att_type(on, att);
  if (not type) {
    return fail(type.error());
  }
  if (not *type) {
    return std::optional<std::vector<std::int64_t>>{};
  }
  const auto refuse = [&]() -> Result {
    return fail(
        nc_fault(file, WrapperFault::type_mismatch, NcOp::get_att, object));
  };
  if (not is_signed_integer(**type)) {
    return refuse();
  }
  return nc::dispatch_numeric(
      **type,
      [&]<class T>() -> Result {
        if constexpr (std::is_integral_v<T>) {
          auto values = file.numeric_att<T>(on, att);
          if (not values) {
            return fail(std::move(values).error());
          }
          return values->transform([](const std::vector<T>& v) {
            return std::vector<std::int64_t>(v.begin(), v.end());
          });
        } else {
          return refuse();
        }
      },
      refuse);
}

std::optional<nc::VarInfo> named(const Vars& vars, std::string_view name) {
  const auto it = std::ranges::find_if(
      vars, [name](const nc::VarInfo& v) { return v.name == name; });
  return it == vars.end() ? std::nullopt : std::optional{*it};
}

/// "EPSG:<digits>" (at most 9 digits), positive.
std::optional<core::Epsg> parse_epsg(std::string_view text) {
  constexpr std::string_view prefix = "EPSG:";
  constexpr std::size_t max_digits = 9;
  if (not text.starts_with(prefix)) {
    return std::nullopt;
  }
  const std::string_view digits = text.substr(prefix.size());
  if (digits.empty() or digits.size() > max_digits or
      not std::ranges::all_of(digits,
                              [](char c) { return c >= '0' and c <= '9'; })) {
    return std::nullopt;
  }
  int code = 0;
  for (const char c : digits) {
    code = (code * 10) + (c - '0');
  }
  const auto epsg = core::Epsg::make(code);
  return epsg ? std::optional{*epsg} : std::nullopt;
}

/// A one-value double attribute: nullopt when absent.
std::expected<std::optional<double>, Error> double_att(const nc::File& file,
                                                       nc::NcNameRef var,
                                                       nc::NcNameRef att) {
  auto values = file.numeric_att<double>(var, att);
  if (not values) {
    return fail(std::move(values).error());
  }
  if (*values and (*values)->size() != 1) {
    return fail(nc_fault(file, WrapperFault::count_mismatch, NcOp::get_att,
                         att.view()));
  }
  return values->transform([](const std::vector<double>& v) { return v[0]; });
}

namespace {

/// The CRS of a grid mapping variable without `epsg_code`:
/// latitude_longitude on the WGS 84 ellipsoid; without ellipsoid parameters,
/// assumed to be WGS 84 (warning).
std::expected<Read<core::Epsg>, Error> crs_from_parameters(
    const nc::File& file, const nc::VarInfo& var) {
  auto parts =
      collect([&] { return text_of(file, var.name, "grid_mapping_name"); },
              [&] { return double_att(file, var.name, "semi_major_axis"); },
              [&] { return double_att(file, var.name, "inverse_flattening"); });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  const auto& [mapping, axis, flattening] = *parts;
  const std::string subject{var.name.view()};
  if (mapping.transform(core::detail::trim) != "latitude_longitude" or
      axis.value_or(sn::wgs84_semi_major_axis) != sn::wgs84_semi_major_axis or
      flattening.value_or(sn::wgs84_inverse_flattening) !=
          sn::wgs84_inverse_flattening) {
    return invalid(FormatErrc::unsupported_crs, subject);
  }
  Read<core::Epsg> out{.value = core::Epsg::wgs84(), .warnings = {}};
  if (not axis or not flattening) {
    out.warnings.push_back(
        {.code = WarningCode::crs_assumed, .subject = subject});
  }
  return out;
}

}  // namespace

/// The grid mapping variable's CRS: `epsg_code`, else its parameters. A
/// prime meridian other than Greenwich is refused either way.
std::expected<Read<core::Epsg>, Error> crs_of_mapping(const nc::File& file,
                                                      const nc::VarInfo& var) {
  auto parts = collect([&] { return text_of(file, var.name, "epsg_code"); },
                       [&] {
                         return double_att(file, var.name,
                                           "longitude_of_prime_meridian");
                       });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  const auto& [code, meridian] = *parts;
  if (meridian.value_or(0.0) != 0.0) {
    return invalid(FormatErrc::unsupported_crs, std::string{var.name.view()});
  }
  if (not code) {
    return crs_from_parameters(file, var);
  }
  const auto epsg = parse_epsg(core::detail::trim(*code));
  if (not epsg) {
    return invalid(FormatErrc::unsupported_crs, std::string{var.name.view()});
  }
  return Read<core::Epsg>{.value = *epsg, .warnings = {}};
}

/// The bytes of a char row without its trailing NUL padding.
std::string trimmed(std::string row) {
  while (not row.empty() and row.back() == '\0') {
    row.pop_back();
  }
  return row;
}

std::expected<std::vector<double>, Error> coordinate(const nc::File& file,
                                                     const nc::VarInfo& var,
                                                     const StopToken& stop) {
  auto samples = file.read_samples(var.name, nc::whole(var), stop);
  if (not samples) {
    return std::unexpected{std::move(samples).error()};
  }
  std::vector<double> values;
  values.reserve(samples->size());
  for (std::size_t i = 0; i < samples->size(); ++i) {
    const std::optional<double> x = (*samples)[i].value();
    if (not x) {
      return invalid(FormatErrc::bad_coordinates, std::string{var.name.view()},
                     i);
    }
    values.push_back(*x);
  }
  return values;
}

/// The position of station i from lon/lat in `projector`'s CRS, or WGS 84.
std::expected<Position, Error> place(double latitude, double longitude,
                                     std::optional<Projector>& projector,
                                     std::size_t i) {
  if (not projector) {
    const auto where =
        core::Location::make({.lat = latitude, .lon = longitude});
    if (not where) {
      return invalid(
          FormatErrc::bad_coordinates,
          where.error() == core::LocationError::longitude_out_of_range ? "lon"
                                                                       : "lat",
          i);
    }
    return Position{.location = *where, .native = std::nullopt};
  }
  const auto native = core::NativePoint::make({.x = longitude, .y = latitude},
                                              projector->crs());
  const auto where = projector->to_location({.x = longitude, .y = latitude});
  if (not native or not where) {
    return invalid(FormatErrc::bad_coordinates, "lon, lat", i);
  }
  return Position{.location = *where, .native = *native};
}

Read<std::optional<core::Unit>> parsed_unit(
    const std::optional<std::string>& text) {
  Read<std::optional<core::Unit>> out{.value = std::nullopt, .warnings = {}};
  if (text) {
    out.value = core::parse_unit(*text);
  }
  if (out.value) {
    const auto* other = std::get_if<core::OtherUnit>(&*out.value);
    if (other != nullptr and not core::is_canonical_other(*other)) {
      out.warnings.push_back({.code = WarningCode::unrecognized_unit,
                              .subject = subject_of(other->symbol())});
    }
  }
  return out;
}

/// `meta` with the datum of `vertical_datum`, when it has one it can carry.
Read<core::SeriesMeta> with_datum(core::SeriesMeta meta,
                                  const std::optional<std::string>& text,
                                  std::string_view variable) {
  Read<core::SeriesMeta> out{.value = std::move(meta), .warnings = {}};
  if (not text) {
    return out;
  }
  const auto datum = core::parse_vertical_datum(*text);
  if (not datum) {
    out.warnings.push_back({.code = WarningCode::datum_unknown,
                            .subject = subject_of(datum.error().text)});
    return out;
  }
  if (not *datum) {
    return out;  // "none"
  }
  auto assumed = out.value.assume_datum(**datum);
  if (not assumed) {
    out.warnings.push_back(
        {.code = WarningCode::datum_unknown,
         .subject = std::string{variable} + ":vertical_datum"});
    return out;
  }
  out.value = *std::move(assumed);
  return out;
}

// ---- datum spellings
// ----------------------------------------------------------

namespace {

struct DatumSpelling {
  std::string_view words;  // lower case, single spaces
  std::string_view token;
};

// The names a CF file (geopotential_datum_name, a vertical_datum) gives the
// datums core knows, by the words of their long names. The tokens and the
// aliases NAVD, NGVD, IGLD are parse_vertical_datum's own.
constexpr std::array<DatumSpelling, 22> datum_spellings{{
    {.words = "north american vertical datum of 1988", .token = "NAVD88"},
    {.words = "north american vertical datum 1988", .token = "NAVD88"},
    {.words = "navd 88", .token = "NAVD88"},
    {.words = "national geodetic vertical datum of 1929", .token = "NGVD29"},
    {.words = "national geodetic vertical datum 1929", .token = "NGVD29"},
    {.words = "sea level datum of 1929", .token = "NGVD29"},
    {.words = "ngvd 29", .token = "NGVD29"},
    {.words = "mean lower low water", .token = "MLLW"},
    {.words = "mean low water", .token = "MLW"},
    {.words = "mean higher high water", .token = "MHHW"},
    {.words = "mean high water", .token = "MHW"},
    {.words = "mean tide level", .token = "MTL"},
    {.words = "mean sea level", .token = "MSL"},
    {.words = "international great lakes datum of 1985", .token = "IGLD85"},
    {.words = "international great lakes datum 1985", .token = "IGLD85"},
    {.words = "igld 85", .token = "IGLD85"},
    {.words = "station datum", .token = "STND"},
    {.words = "gage datum", .token = "STND"},
    {.words = "gauge datum", .token = "STND"},
    {.words = "navd88", .token = "NAVD88"},
    {.words = "ngvd29", .token = "NGVD29"},
    {.words = "igld85", .token = "IGLD85"},
}};

/// `text` in lower case with every run of characters that are not letters or
/// digits one space.
std::string datum_words(std::string_view text) {
  std::string out;
  bool gap = false;
  for (const char c : text) {
    const bool alnum = (c >= 'a' and c <= 'z') or (c >= 'A' and c <= 'Z') or
                       (c >= '0' and c <= '9');
    if (alnum) {
      if (gap and not out.empty()) {
        out.push_back(' ');
      }
      gap = false;
      out.push_back(c >= 'A' and c <= 'Z' ? static_cast<char>(c - 'A' + 'a')
                                          : c);
    } else {
      gap = true;
    }
  }
  return out;
}

}  // namespace

std::string datum_text(std::string_view text) {
  const std::string words = datum_words(text);
  const auto known =
      std::ranges::find(datum_spellings, words, &DatumSpelling::words);
  if (known != datum_spellings.end()) {
    return std::string{known->token};
  }
  return std::string{core::detail::trim(text)};
}

// ---- series whose times are not strictly increasing (decision 30.3)
// -----------

core::NormalizeReport normalize_columns(
    core::TimeAxis& times, std::span<core::Column* const> columns) {
  const auto differs = [&columns](std::size_t kept, std::size_t dropped) {
    return std::ranges::any_of(columns, [kept, dropped](const core::Column* c) {
      return (*c)[kept] != (*c)[dropped];
    });
  };
  const core::NormalizingOrder order = core::normalizing_order(times, differs);
  if (order.report.clean()) {
    return order.report;
  }
  const auto pick = [&order](auto& values) {
    std::remove_reference_t<decltype(values)> out;
    out.reserve(order.kept.size());
    for (const std::size_t row : order.kept) {
      out.push_back(values[row]);
    }
    values = std::move(out);
  };
  pick(times);
  for (core::Column* column : columns) {
    pick(*column);
  }
  return order.report;
}

void merge_reports(core::NormalizeReport& sum,
                   const core::NormalizeReport& more) {
  sum.descents += more.descents;
  sum.duplicates_dropped += more.duplicates_dropped;
  sum.conflicting_duplicates += more.conflicting_duplicates;
}

void add_normalize_warnings(std::vector<Warning>& warnings,
                            const core::NormalizeReport& report,
                            std::string_view subject) {
  const std::string text = subject_of(subject);
  append_if_counted(warnings, {.code = WarningCode::times_reordered,
                               .subject = text,
                               .count = report.descents});
  append_if_counted(warnings, {.code = WarningCode::duplicate_times_dropped,
                               .subject = text,
                               .count = report.duplicates_dropped});
  append_if_counted(warnings, {.code = WarningCode::conflicting_duplicate_times,
                               .subject = text,
                               .count = report.conflicting_duplicates});
}

}  // namespace mov::io::detail::station_nc
