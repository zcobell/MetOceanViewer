// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// What every USGS page reader shares (docs/providers-design.md section
// 4.3): the envelope of a GeoJSON FeatureCollection page (numberReturned,
// the links and the next one), the start of each feature, and the members
// its rows read. Private to src/io/json/.

#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mov/io/detail/json.hpp"
#include "mov/io/detail/usgs_json.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/usgs.hpp"

namespace mov::io::wire::usgs::page {

namespace json = io::detail::json;

// ---- what is kept of the envelope
// -------------------------------------------------

inline constexpr std::array link_fields{
    json::Field{.name = "rel", .keep = &json::leaf},
    json::Field{.name = "href", .keep = &json::leaf}};
inline constexpr json::Keep link_schema{.fields = link_fields};
inline constexpr json::Keep links_schema{
    .element = &link_schema, .max_elements = io::detail::usgs::max_links};

/// The members of a page besides `features`, which each reader names with
/// its own feature schema.
inline constexpr json::Field number_returned_field{.name = "numberReturned",
                                                   .keep = &json::leaf};
inline constexpr json::Field links_field{.name = "links",
                                         .keep = &links_schema};

// ---- the envelope
// ------------------------------------------------------------------

/// The paging of a parsed page that streamed `features` features: the
/// `features` array is required; `numberReturned`, when present, must be
/// that count (count_mismatch); the `next` link (at most one) is a Verbatim
/// pinned to `collection`'s items path, or none when it carries `offset`
/// without `cursor` (the API's link after a full last page).
[[nodiscard]] std::expected<Paging, Error> read_paging(std::string_view body,
                                                       const json::Value& root,
                                                       std::size_t features,
                                                       Collection collection);

// ---- features
// ----------------------------------------------------------------------

/// The cost of a feature against ReadLimits::max_elements: one, before it
/// is read.
[[nodiscard]] std::expected<void, Error> check_room(std::string_view body,
                                                    std::size_t index,
                                                    const json::Value& feature,
                                                    const ReadContext& ctx);

/// The `properties` object of a feature: bad_member at the feature when it
/// is no object or has none, at the member when it is no object.
[[nodiscard]] std::expected<const json::Value*, Error> properties_of(
    std::string_view body, const json::Value& feature);

/// A ParseError `code` at `offset`.
[[nodiscard]] std::unexpected<Error> fail_at(std::string_view body,
                                             ParseErrc code,
                                             std::size_t offset);

/// One past a string value's opening quote: where its text starts.
[[nodiscard]] constexpr std::size_t text_start(
    const json::Value& string) noexcept {
  return string.offset + 1;
}

// ---- counting what samples say
// ----------------------------------------------------

/// Counts by text with at most `max_subjects` distinct texts; any further
/// one is counted under "...". A hostile page cannot make a warning per
/// sample.
class TextCounts {
 public:
  static constexpr std::size_t max_subjects = 16;

  void add(std::string_view text, std::size_t count = 1);
  /// One warning of `code` per text, in text order, "..." last.
  void report(std::vector<Warning>& warnings, WarningCode code) const;

 private:
  std::map<std::string, std::size_t, std::less<>> counts_;
  std::size_t others_ = 0;
};

/// What the samples of a data page say about themselves: "Provisional"
/// approval and qualifiers.
class Marks {
 public:
  /// Reads `approval_status` and `qualifier` of a feature's properties:
  /// absent or null is nothing; a string, or for the qualifier an array of
  /// strings; anything else is bad_member.
  [[nodiscard]] std::expected<void, Error> add(std::string_view body,
                                               const json::Value& properties);
  void report(std::vector<Warning>& warnings) const;

 private:
  std::size_t provisional_ = 0;
  TextCounts qualifiers_;
};

}  // namespace mov::io::wire::usgs::page
