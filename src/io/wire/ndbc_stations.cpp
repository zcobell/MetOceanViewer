// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// ndbc::read_active_stations: the scanned activestations.xml to station
// records (docs/providers-design.md section 4.5).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/core/geo.hpp"
#include "mov/core/provider/capabilities.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/io/detail/ndbc_xml.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/ndbc.hpp"

namespace mov::io::wire::ndbc {

namespace {

namespace xml = io::detail::ndbc_xml;

// Why a station is left out, in the order the warnings are reported.
enum class Skip : std::uint8_t {
  id,
  duplicate,
  position,
  elevation,
  text,
  feeds
};
constexpr std::array<std::string_view, 6> skip_subjects{
    "id", "duplicate", "position", "elevation", "text", "feeds"};

[[nodiscard]] const std::string* attribute(const xml::Element& element,
                                           std::string_view name) {
  const auto found =
      std::ranges::find(element.attributes, name, &xml::Attribute::name);
  return found == element.attributes.end() ? nullptr : &found->value;
}

[[nodiscard]] std::optional<double> number(const std::string* text) {
  if (text == nullptr) {
    return std::nullopt;
  }
  const auto value = io::detail::parse_double(*text);
  return value ? std::optional<double>{*value} : std::nullopt;
}

// A text attribute, or the empty text when it is absent.
[[nodiscard]] std::optional<core::StationText> text_of(
    const xml::Element& element, std::string_view name) {
  const std::string* value = attribute(element, name);
  if (value == nullptr) {
    return core::StationText{};
  }
  auto text = core::StationText::make(*value);
  return text ? std::optional<core::StationText>{*std::move(text)}
              : std::nullopt;
}

// A feed flag: "y" or "n", nullopt when absent; anything else skips the
// station.
using Flag = std::expected<std::optional<bool>, Skip>;

[[nodiscard]] Flag flag_of(const xml::Element& element, std::string_view name) {
  const std::string* value = attribute(element, name);
  if (value == nullptr) {
    return std::optional<bool>{};
  }
  if (*value != "y" and *value != "n") {
    return std::unexpected{Skip::feeds};
  }
  return std::optional<bool>{*value == "y"};
}

[[nodiscard]] std::expected<Feeds, Skip> feeds_of(const xml::Element& e) {
  const std::array flags{flag_of(e, "met"), flag_of(e, "currents"),
                         flag_of(e, "waterquality"), flag_of(e, "dart")};
  if (const auto bad = std::ranges::find_if_not(
          flags, [](const Flag& f) { return f.has_value(); });
      bad != flags.end()) {
    return std::unexpected{bad->error()};
  }
  return Feeds{.met = *flags[0],
               .currents = *flags[1],
               .water_quality = *flags[2],
               .dart = *flags[3]};
}

[[nodiscard]] std::expected<core::Location, Skip> location_of(
    const xml::Element& e) {
  const auto lat = number(attribute(e, "lat"));
  const auto lon = number(attribute(e, "lon"));
  if (not lat or not lon) {
    return std::unexpected{Skip::position};
  }
  return core::Location::make({.lat = *lat, .lon = *lon})
      .transform_error([](core::LocationError) { return Skip::position; });
}

// The elevation in metres above mean sea level; absent is nullopt.
[[nodiscard]] std::expected<std::optional<core::Length>, Skip> elevation_of(
    const xml::Element& e) {
  const std::string* text = attribute(e, "elev");
  if (text == nullptr) {
    return std::optional<core::Length>{};
  }
  const auto metres = number(text);
  if (not metres) {
    return std::unexpected{Skip::elevation};
  }
  return std::optional<core::Length>{
      core::Length::in(*metres, core::LengthUnit::meter)};
}

[[nodiscard]] std::expected<core::Capabilities<core::provider::Ndbc>, Skip>
capabilities_of(const xml::Element& e) {
  auto owner = text_of(e, "owner");
  auto program = text_of(e, "pgm");
  auto platform = text_of(e, "type");
  if (not owner or not program or not platform) {
    return std::unexpected{Skip::text};
  }
  return elevation_of(e).transform([&](std::optional<core::Length> elevation) {
    return core::Capabilities<core::provider::Ndbc>{
        .owner = *std::move(owner),
        .program = *std::move(program),
        .platform_type = *std::move(platform),
        .elevation = elevation};
  });
}

// One station: everything but the duplicate check.
[[nodiscard]] std::expected<StationRecord, Skip> record_of(
    const xml::Element& e) {
  const std::string* raw_id = attribute(e, "id");
  if (raw_id == nullptr) {
    return std::unexpected{Skip::id};
  }
  auto id = Station::make(*raw_id);
  if (not id) {
    return std::unexpected{Skip::id};
  }
  const auto location = location_of(e);
  if (not location) {
    return std::unexpected{location.error()};
  }
  auto name = text_of(e, "name");
  if (not name) {
    return std::unexpected{Skip::text};
  }
  auto caps = capabilities_of(e);
  if (not caps) {
    return std::unexpected{caps.error()};
  }
  const auto feeds = feeds_of(e);
  if (not feeds) {
    return std::unexpected{feeds.error()};
  }
  return StationRecord{.station = {.id = *std::move(id),
                                   .name = *std::move(name),
                                   .location = *location,
                                   .validity = core::ValidRange{}},
                       .caps = *std::move(caps),
                       .feeds = *feeds};
}

[[nodiscard]] Read<std::vector<StationRecord>> records_of(
    const std::vector<xml::Element>& elements) {
  Read<std::vector<StationRecord>> out{.value = {}, .warnings = {}};
  out.value.reserve(elements.size());
  std::array<std::size_t, skip_subjects.size()> skipped{};
  std::set<std::string, std::less<>> seen;
  for (const xml::Element& e : elements) {
    auto record = record_of(e);
    if (record and not seen.emplace(record->station.id.value()).second) {
      record = std::unexpected{Skip::duplicate};
    }
    if (not record) {
      ++skipped.at(static_cast<std::size_t>(record.error()));
      continue;
    }
    out.value.push_back(*std::move(record));
  }
  for (std::size_t i = 0; i < skipped.size(); ++i) {
    append_if_counted(out.warnings, {.code = WarningCode::station_skipped,
                                     .subject = std::string{skip_subjects[i]},
                                     .count = skipped.at(i)});
  }
  return out;
}

}  // namespace

std::expected<Read<std::vector<StationRecord>>, Error> read_active_stations(
    std::string_view text, const ReadContext& ctx) {
  return xml::scan(text, ctx).transform([](const xml::Document& document) {
    return records_of(document.stations);
  });
}

}  // namespace mov::io::wire::ndbc
