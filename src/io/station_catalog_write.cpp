// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Writing a station catalog: its canonical text (docs/station-catalog.md
// section 7).

#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <ios>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "mov/core/datum.hpp"
#include "mov/core/enum_set.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/provider/capabilities.hpp"
#include "mov/core/provider/coops.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/core/utf8.hpp"
#include "mov/io/detail/json_write.hpp"
#include "mov/io/detail/reporting.hpp"
#include "mov/io/detail/station_catalog_text.hpp"
#include "mov/io/error.hpp"
#include "mov/io/station_catalog.hpp"
#include "mov/io/text_file.hpp"

namespace mov::io {

namespace {

namespace tokens = detail::catalog;
namespace jt = detail::json_text;
using core::provider::Coops;
using core::provider::Ndbc;
using core::provider::Usgs;

using Checked = std::expected<void, FormatError>;

[[nodiscard]] FormatError refuse(FormatErrc code, std::string_view subject) {
  return detail::format_error(code, std::string{subject});
}

// ---- what the format cannot hold
// -------------------------------------------

Checked check_validity(const core::ValidRange& validity,
                       std::string_view subject) {
  for (const auto day : {validity.first(), validity.last()}) {
    if (day and not jt::four_digit_year(*day)) {
      return std::unexpected{refuse(FormatErrc::time_out_of_range, subject)};
    }
  }
  return {};
}

Checked check_header(const CatalogHeader& header) {
  if (not jt::four_digit_year(header.generated)) {
    return std::unexpected{refuse(FormatErrc::time_out_of_range, "generated")};
  }
  for (const CatalogSource& source : header.sources) {
    if (not jt::four_digit_year(source.retrieved)) {
      return std::unexpected{refuse(FormatErrc::time_out_of_range, "sources")};
    }
  }
  return {};
}

Checked check_caps(const core::Capabilities<Coops>& /*caps*/,
                   std::string_view /*id*/) {
  return {};
}

Checked check_caps(const core::Capabilities<Ndbc>& /*caps*/,
                   std::string_view /*id*/) {
  return {};
}

Checked check_caps(const core::Capabilities<Usgs>& caps, std::string_view id) {
  for (const core::usgs::SeriesInfo& s : caps.series.items()) {
    if (not core::is_valid_utf8(s.parameter_name)) {
      return std::unexpected{refuse(FormatErrc::bad_encoding, id)};
    }
    if (s.unit and core::parse_unit(core::udunits(*s.unit)) != s.unit) {
      return std::unexpected{refuse(FormatErrc::noncanonical_unit, id)};
    }
    if (auto ok = check_validity(s.validity, id); not ok) {
      return ok;
    }
  }
  return {};
}

template <CatalogProvider P>
Checked check(const Catalog<P>& catalog) {
  if (auto ok = check_header(catalog.header()); not ok) {
    return ok;
  }
  for (const CatalogEntry<P>& e : catalog.entries()) {
    const std::string_view id = e.station.id.value();
    if (auto ok = check_validity(e.station.validity, id); not ok) {
      return ok;
    }
    if (auto ok = check_caps(e.caps, id); not ok) {
      return ok;
    }
  }
  return {};
}

// ---- text
// -----------------------------------------------------------------------

// Writes the members of one object: `,` between them.
class Members {
 public:
  explicit Members(std::string& out) : out_{out} { out_.push_back('{'); }
  Members(const Members&) = delete;
  Members& operator=(const Members&) = delete;
  Members(Members&&) = delete;
  Members& operator=(Members&&) = delete;
  ~Members() = default;

  // Starts member `name`; the caller appends its value.
  std::string& member(std::string_view name) {
    if (not first_) {
      out_.push_back(',');
    }
    first_ = false;
    jt::append_string(out_, name);
    out_.push_back(':');
    return out_;
  }

  void close() { out_.push_back('}'); }

 private:
  std::string& out_;
  bool first_ = true;
};

// `["a","b"]` of the set's members, in enumerator order.
template <class E, class Token>
void append_set(std::string& out, const core::EnumSet<E>& set, Token token_of) {
  out.push_back('[');
  bool first = true;
  for (const E e : core::Enumerators<E>::values) {
    if (set.contains(e)) {
      if (not first) {
        out.push_back(',');
      }
      first = false;
      jt::append_string(out, token_of(e));
    }
  }
  out.push_back(']');
}

void append_validity(Members& m, const core::ValidRange& validity) {
  if (const auto first = validity.first()) {
    jt::append_date(m.member("first"), *first);
  }
  if (const auto last = validity.last()) {
    jt::append_date(m.member("last"), *last);
  }
}

void append_height(std::string& out, core::Length height) {
  jt::append_fixed(out, height.as(core::LengthUnit::meter),
                   tokens::height_decimals);
}

void append_block(std::string& out, const core::Capabilities<Coops>& caps) {
  Members m{out};
  if (not caps.observed.empty()) {
    append_set(m.member("observed"), caps.observed,
               [](core::coops::Observed o) {
                 return tokens::token_of(tokens::observed_tokens, o);
               });
  }
  if (not caps.met.empty()) {
    append_set(m.member("met"), caps.met, [](core::coops::Met x) {
      return tokens::token_of(tokens::met_tokens, x);
    });
  }
  if (caps.air_gap) {
    m.member("air_gap") += "true";
  }
  if (caps.predictions != core::coops::Predictions::none) {
    jt::append_string(
        m.member("predictions"),
        tokens::token_of(tokens::predictions_tokens, caps.predictions));
  }
  if (not caps.datums_offered.empty()) {
    append_set(m.member("datums_offered"), caps.datums_offered,
               tokens::datum_token);
  }
  if (caps.datums) {
    Members heights{m.member("datums")};
    for (const core::VerticalDatum d :
         core::Enumerators<core::VerticalDatum>::values) {
      const auto h = caps.datums->height_above_msl(d);
      if (h and d != core::VerticalDatum::msl) {
        append_height(heights.member(core::to_string(d)), *h);
      }
    }
    heights.close();
  }
  m.close();
}

void append_block(std::string& out, const core::Capabilities<Usgs>& caps) {
  Members m{out};
  if (not caps.series.empty()) {
    std::string& list = m.member("series");
    list.push_back('[');
    bool first = true;
    for (const core::usgs::SeriesInfo& s : caps.series.items()) {
      if (not first) {
        list.push_back(',');
      }
      first = false;
      Members row{list};
      jt::append_string(row.member("id"), s.id.view());
      jt::append_string(row.member("parameter"), s.parameter.view());
      jt::append_string(row.member("statistic"), s.statistic.view());
      if (not s.parameter_name.empty()) {
        jt::append_string(row.member("parameter_name"), s.parameter_name);
      }
      if (s.primary) {
        row.member("primary") += "true";
      }
      if (s.unit) {
        jt::append_string(row.member("unit"), core::udunits(*s.unit));
      }
      append_validity(row, s.validity);
      row.close();
    }
    list.push_back(']');
  }
  m.close();
}

void append_block(std::string& out, const core::Capabilities<Ndbc>& caps) {
  Members m{out};
  const auto text_member = [&m](std::string_view name,
                                const core::StationText& value) {
    if (not value.empty()) {
      jt::append_string(m.member(name), value.view());
    }
  };
  text_member("owner", caps.owner);
  text_member("program", caps.program);
  text_member("platform_type", caps.platform_type);
  if (caps.elevation) {
    append_height(m.member("elevation_m"), tokens::canonical(*caps.elevation));
  }
  m.close();
}

// The member name of each provider's block.
[[nodiscard]] constexpr std::string_view block_name(Coops /*p*/) noexcept {
  return "coops";
}
[[nodiscard]] constexpr std::string_view block_name(Usgs /*p*/) noexcept {
  return "usgs";
}
[[nodiscard]] constexpr std::string_view block_name(Ndbc /*p*/) noexcept {
  return "ndbc";
}

template <CatalogProvider P>
void append_station(std::string& out, const CatalogEntry<P>& e) {
  Members m{out};
  jt::append_string(m.member("id"), e.station.id.value());
  jt::append_string(m.member("name"), e.station.name.view());
  const core::Location at = tokens::canonical(e.station.location);
  jt::append_fixed(m.member("lon"), at.lon(), tokens::position_decimals);
  jt::append_fixed(m.member("lat"), at.lat(), tokens::position_decimals);
  append_validity(m, e.station.validity);
  append_block(m.member(block_name(P{})), e.caps);
  m.close();
}

template <CatalogProvider P>
std::string text_of(const Catalog<P>& catalog) {
  std::string out = "{";
  jt::append_string(out, "format");
  out.push_back(':');
  jt::append_string(out, catalog_format);
  out += ",\"version\":";
  jt::append_string(
      out, std::to_string(catalog_major) + "." + std::to_string(catalog_minor));
  out += ",\"provider\":";
  jt::append_string(out, core::to_token(P::source));
  out += ",\n \"generated\":";
  jt::append_timestamp(out, catalog.header().generated);
  out += ",\n \"sources\":[";
  bool first = true;
  for (const CatalogSource& source : catalog.header().sources) {
    if (not first) {
      out.push_back(',');
    }
    first = false;
    Members m{out};
    jt::append_string(m.member("url"), source.url.view());
    jt::append_timestamp(m.member("retrieved"), source.retrieved);
    m.close();
  }
  out += "],\n \"stations\":[";
  first = true;
  for (const CatalogEntry<P>& e : catalog.entries()) {
    out += first ? "\n" : ",\n";
    first = false;
    append_station(out, e);
  }
  out += catalog.empty() ? "]}\n" : "\n]}\n";
  return out;
}

}  // namespace

std::expected<std::string, FormatError> format_station_catalog(
    const StationCatalog& catalog) {
  return std::visit(
      [](const auto& c) -> std::expected<std::string, FormatError> {
        return check(c).transform([&c] { return text_of(c); });
      },
      catalog);
}

std::expected<void, Error> write_station_catalog(
    const std::filesystem::path& path, const StationCatalog& catalog) {
  auto bytes = format_station_catalog(catalog);
  if (not bytes) {
    return detail::fail(std::move(bytes).error());
  }
  return write_file_atomic(
      path, [&bytes](std::ostream& out) -> std::expected<void, FileError> {
        out.write(bytes->data(), static_cast<std::streamsize>(bytes->size()));
        return {};
      });
}

}  // namespace mov::io
