// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/core/datum.hpp.

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>

#include "mov/core/datum.hpp"
#include "test_helpers.hpp"

using mov::core::ConflictingHeight;
using mov::core::DatumHeight;
using mov::core::DatumTable;
using mov::core::DatumTableError;
using mov::core::Length;
using mov::core::LengthUnit;
using mov::core::MissingMsl;
using mov::core::MissingOffset;
using mov::core::NonFiniteHeight;
using mov::core::parse_vertical_datum;
using mov::core::to_string;
using mov::core::UnknownDatum;
using mov::core::VerticalDatum;
using mov::test::infinity;

namespace {

constexpr std::array all_datums{VerticalDatum::mhhw,   VerticalDatum::mhw,
                                VerticalDatum::mtl,    VerticalDatum::msl,
                                VerticalDatum::mlw,    VerticalDatum::mllw,
                                VerticalDatum::navd88, VerticalDatum::ngvd29,
                                VerticalDatum::igld85, VerticalDatum::stnd};

constexpr Length metres(double v) { return Length::in(v, LengthUnit::meter); }

constexpr DatumHeight height(VerticalDatum d, double v) {
  return {.datum = d, .height = metres(v)};
}

constexpr std::expected<DatumTable, DatumTableError> table(
    VerticalDatum reference, std::span<const DatumHeight> heights) {
  return DatumTable::from_heights(reference, heights);
}

// Heights of the sample station, metres above MSL; all dyadic, so every
// sum and difference below is exact.
constexpr std::array dyadic_heights{
    height(VerticalDatum::mhhw, 0.75),    height(VerticalDatum::mhw, 0.5),
    height(VerticalDatum::mtl, 0.0625),   height(VerticalDatum::mlw, -0.5),
    height(VerticalDatum::mllw, -0.75),   height(VerticalDatum::ngvd29, -0.25),
    height(VerticalDatum::navd88, 0.125),
};

// The same station, written relative to MLLW.
constexpr std::array from_mllw{
    height(VerticalDatum::mhhw, 1.5),     height(VerticalDatum::mhw, 1.25),
    height(VerticalDatum::mtl, 0.8125),   height(VerticalDatum::msl, 0.75),
    height(VerticalDatum::mlw, 0.25),     height(VerticalDatum::ngvd29, 0.5),
    height(VerticalDatum::navd88, 0.875),
};

constexpr bool is_no_datum(std::string_view text) {
  const auto parsed = parse_vertical_datum(text);
  return parsed.has_value() and not parsed->has_value();
}

constexpr bool is_unknown(std::string_view text) {
  return not parse_vertical_datum(text).has_value();
}

constexpr bool tokens_round_trip() {
  return std::ranges::all_of(all_datums, [](VerticalDatum d) {
    return parse_vertical_datum(to_string(d)) == d;
  });
}

constexpr bool offset_is_zero_everywhere(const DatumTable& t) {
  return std::ranges::all_of(all_datums, [&t](VerticalDatum d) {
    const auto o = t.offset(d, d);
    return o and *o == Length{};
  });
}

// offset(a, b) == -offset(b, a), and offset(a, b) + offset(b, c) ==
// offset(a, c), for every triple the table knows.
constexpr bool offsets_form_a_group(const DatumTable& t) {
  for (const VerticalDatum a : all_datums) {
    for (const VerticalDatum b : all_datums) {
      for (const VerticalDatum c : all_datums) {
        const auto ab = t.offset(a, b);
        const auto ba = t.offset(b, a);
        const auto bc = t.offset(b, c);
        const auto ac = t.offset(a, c);
        if (ab and ba and not(*ab == -*ba)) {
          return false;
        }
        if (ab and bc and ac and not(*ab + *bc == *ac)) {
          return false;
        }
      }
    }
  }
  return true;
}

}  // namespace

TEST_CASE("datum types are value types", "[core][datum][constexpr]") {
  STATIC_REQUIRE(std::regular<VerticalDatum>);
  STATIC_REQUIRE(std::regular<DatumTable>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<DatumTable>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<DatumTable>);
  STATIC_REQUIRE(std::regular<UnknownDatum>);
  STATIC_REQUIRE(std::regular<DatumHeight>);
  STATIC_REQUIRE(std::regular<MissingMsl>);
  STATIC_REQUIRE(std::regular<ConflictingHeight>);
  STATIC_REQUIRE(std::regular<NonFiniteHeight>);
  STATIC_REQUIRE(std::regular<MissingOffset>);
  STATIC_REQUIRE(std::regular<DatumTableError>);
  STATIC_REQUIRE(std::regular<std::optional<VerticalDatum>>);
}

TEST_CASE("VerticalDatum tokens", "[core][datum][constexpr]") {
  STATIC_REQUIRE(to_string(VerticalDatum::mhhw) == "MHHW");
  STATIC_REQUIRE(to_string(VerticalDatum::mhw) == "MHW");
  STATIC_REQUIRE(to_string(VerticalDatum::mtl) == "MTL");
  STATIC_REQUIRE(to_string(VerticalDatum::msl) == "MSL");
  STATIC_REQUIRE(to_string(VerticalDatum::mlw) == "MLW");
  STATIC_REQUIRE(to_string(VerticalDatum::mllw) == "MLLW");
  STATIC_REQUIRE(to_string(VerticalDatum::navd88) == "NAVD88");
  STATIC_REQUIRE(to_string(VerticalDatum::ngvd29) == "NGVD29");
  STATIC_REQUIRE(to_string(VerticalDatum::igld85) == "IGLD85");
  STATIC_REQUIRE(to_string(VerticalDatum::stnd) == "STND");
}

TEST_CASE("every token round-trips", "[core][datum][constexpr]") {
  STATIC_REQUIRE(tokens_round_trip());
  STATIC_REQUIRE(parse_vertical_datum("MHHW") == VerticalDatum::mhhw);
  STATIC_REQUIRE(parse_vertical_datum("MLLW") == VerticalDatum::mllw);
  STATIC_REQUIRE(parse_vertical_datum("NAVD88") == VerticalDatum::navd88);
  STATIC_REQUIRE(parse_vertical_datum("IGLD85") == VerticalDatum::igld85);
}

// v4's datumID("MHW") fell through to NullDatum.
TEST_CASE("MHW parses", "[core][datum][constexpr][regression][N8]") {
  STATIC_REQUIRE(parse_vertical_datum("MHW") == VerticalDatum::mhw);
  STATIC_REQUIRE(parse_vertical_datum("mhw") == VerticalDatum::mhw);
  STATIC_REQUIRE(parse_vertical_datum(to_string(VerticalDatum::mhw)) ==
                 VerticalDatum::mhw);
}

TEST_CASE("datum parsing is case-insensitive and knows the aliases",
          "[core][datum][constexpr]") {
  STATIC_REQUIRE(parse_vertical_datum("mllw") == VerticalDatum::mllw);
  STATIC_REQUIRE(parse_vertical_datum("Mllw") == VerticalDatum::mllw);
  STATIC_REQUIRE(parse_vertical_datum("Stnd") == VerticalDatum::stnd);
  STATIC_REQUIRE(parse_vertical_datum("stnd") == VerticalDatum::stnd);
  STATIC_REQUIRE(parse_vertical_datum("NAVD") == VerticalDatum::navd88);
  STATIC_REQUIRE(parse_vertical_datum("navd") == VerticalDatum::navd88);
  STATIC_REQUIRE(parse_vertical_datum("NGVD") == VerticalDatum::ngvd29);
  STATIC_REQUIRE(parse_vertical_datum("IGLD") == VerticalDatum::igld85);
  STATIC_REQUIRE(parse_vertical_datum("igld") == VerticalDatum::igld85);
  STATIC_REQUIRE(parse_vertical_datum("  MSL\t") == VerticalDatum::msl);
}

TEST_CASE("no datum is an engaged nullopt, not an error",
          "[core][datum][constexpr]") {
  STATIC_REQUIRE(is_no_datum(""));
  STATIC_REQUIRE(is_no_datum("  "));
  STATIC_REQUIRE(is_no_datum("\t\n"));
  STATIC_REQUIRE(is_no_datum("none"));
  STATIC_REQUIRE(is_no_datum("None"));
  STATIC_REQUIRE(is_no_datum("NONE"));
  STATIC_REQUIRE(is_no_datum("  none "));
}

TEST_CASE("unknown text is an UnknownDatum carrying the trimmed text",
          "[core][datum][constexpr]") {
  // Not guessed, and not confused with "no datum".
  STATIC_REQUIRE(is_unknown("MLLWX"));
  STATIC_REQUIRE(is_unknown("ML LW"));
  STATIC_REQUIRE(is_unknown("NAVD8"));
  STATIC_REQUIRE(is_unknown("LWI"));
  STATIC_REQUIRE(is_unknown("nonesuch"));
  STATIC_REQUIRE(is_unknown(std::string_view{"MSL\0", 4}));
  STATIC_REQUIRE_FALSE(is_no_datum("MLLWX"));
  STATIC_REQUIRE_FALSE(is_unknown("MLLW"));
  STATIC_REQUIRE_FALSE(is_unknown(""));
  STATIC_REQUIRE_FALSE(is_unknown("none"));
  STATIC_REQUIRE(parse_vertical_datum("  MLLWX \t").error() ==
                 UnknownDatum{.text = "MLLWX"});
  STATIC_REQUIRE(parse_vertical_datum("LWI").error().text == "LWI");
  // The text is a view of the argument, trimmed but not otherwise changed.
  constexpr std::string_view input = "  Odd Case  ";
  STATIC_REQUIRE(parse_vertical_datum(input).error().text.data() ==
                 input.data() + 2);
}

TEST_CASE("from_heights relative to MSL stores heights as given",
          "[core][datum][constexpr]") {
  constexpr auto t = table(VerticalDatum::msl, dyadic_heights);
  STATIC_REQUIRE(t.has_value());
  STATIC_REQUIRE(t->height_above_msl(VerticalDatum::mhhw) == metres(0.75));
  STATIC_REQUIRE(t->height_above_msl(VerticalDatum::mllw) == metres(-0.75));
  STATIC_REQUIRE(t->height_above_msl(VerticalDatum::navd88) == metres(0.125));
  // MSL is not stored: it is zero by definition.
  STATIC_REQUIRE(t->height_above_msl(VerticalDatum::msl) == metres(0.0));
  // Not supplied: unknown, not zero.
  STATIC_REQUIRE_FALSE(t->height_above_msl(VerticalDatum::stnd).has_value());
  STATIC_REQUIRE_FALSE(t->height_above_msl(VerticalDatum::igld85).has_value());
}

TEST_CASE("from_heights normalizes a table given relative to another datum",
          "[core][datum][constexpr]") {
  constexpr auto t = table(VerticalDatum::mllw, from_mllw);
  STATIC_REQUIRE(t.has_value());
  // The same station as dyadic_heights, whatever the reference.
  STATIC_REQUIRE(*t == *table(VerticalDatum::msl, dyadic_heights));
  STATIC_REQUIRE(t->height_above_msl(VerticalDatum::mllw) == metres(-0.75));
  STATIC_REQUIRE(t->height_above_msl(VerticalDatum::mhhw) == metres(0.75));
  STATIC_REQUIRE(t->height_above_msl(VerticalDatum::msl) == metres(0.0));
}

TEST_CASE("from_heights of nothing", "[core][datum][constexpr]") {
  // Relative to MSL, nothing is a table that knows only MSL.
  constexpr auto empty = table(VerticalDatum::msl, {});
  STATIC_REQUIRE(empty.has_value());
  STATIC_REQUIRE(*empty == DatumTable{});
  STATIC_REQUIRE(DatumTable{}.height_above_msl(VerticalDatum::msl) ==
                 metres(0.0));
  STATIC_REQUIRE_FALSE(
      DatumTable{}.height_above_msl(VerticalDatum::mllw).has_value());
  // Relative to anything else, MSL's own height is needed.
  STATIC_REQUIRE(std::holds_alternative<MissingMsl>(
      table(VerticalDatum::mllw, {}).error()));
}

TEST_CASE("from_heights without MSL is an error", "[core][datum][constexpr]") {
  constexpr std::array no_msl{height(VerticalDatum::mhhw, 1.5),
                              height(VerticalDatum::mlw, 0.25)};
  STATIC_REQUIRE(table(VerticalDatum::mllw, no_msl).error() ==
                 DatumTableError{MissingMsl{}});
  // Giving the reference its own height does not help.
  constexpr std::array only_reference{height(VerticalDatum::mllw, 0.0)};
  STATIC_REQUIRE(table(VerticalDatum::mllw, only_reference).error() ==
                 DatumTableError{MissingMsl{}});
}

TEST_CASE("from_heights accepts equal duplicates", "[core][datum][constexpr]") {
  constexpr std::array twice{
      height(VerticalDatum::mhhw, 0.75), height(VerticalDatum::mllw, -0.75),
      height(VerticalDatum::mhhw, 0.75), height(VerticalDatum::msl, 0.0),
      height(VerticalDatum::mllw, -0.75)};
  constexpr auto t = table(VerticalDatum::msl, twice);
  STATIC_REQUIRE(t.has_value());
  STATIC_REQUIRE(t->height_above_msl(VerticalDatum::mhhw) == metres(0.75));
  // The reference's own height is 0, so restating it is a duplicate.
  constexpr std::array restated{height(VerticalDatum::mllw, 0.0),
                                height(VerticalDatum::msl, 0.75)};
  STATIC_REQUIRE(table(VerticalDatum::mllw, restated).has_value());
}

TEST_CASE("from_heights rejects conflicting heights",
          "[core][datum][constexpr]") {
  constexpr std::array conflict{height(VerticalDatum::mhhw, 0.75),
                                height(VerticalDatum::mllw, -0.75),
                                height(VerticalDatum::mhhw, 0.8125)};
  STATIC_REQUIRE(table(VerticalDatum::msl, conflict).error() ==
                 DatumTableError{ConflictingHeight{VerticalDatum::mhhw}});
  // MSL is the reference here, so its height is 0.
  constexpr std::array msl_above_itself{height(VerticalDatum::msl, 0.25)};
  STATIC_REQUIRE(table(VerticalDatum::msl, msl_above_itself).error() ==
                 DatumTableError{ConflictingHeight{VerticalDatum::msl}});
  // The reference is 0 above itself, whatever its row says.
  constexpr std::array reference_above_itself{height(VerticalDatum::mllw, 0.25),
                                              height(VerticalDatum::msl, 1.0)};
  STATIC_REQUIRE(table(VerticalDatum::mllw, reference_above_itself).error() ==
                 DatumTableError{ConflictingHeight{VerticalDatum::mllw}});
  // The first problem in the input is the one reported.
  constexpr std::array two_conflicts{
      height(VerticalDatum::mhw, 1.0), height(VerticalDatum::mlw, 2.0),
      height(VerticalDatum::mlw, 3.0), height(VerticalDatum::mhw, 4.0)};
  STATIC_REQUIRE(table(VerticalDatum::msl, two_conflicts).error() ==
                 DatumTableError{ConflictingHeight{VerticalDatum::mlw}});
}

TEST_CASE("from_heights rejects infinite heights", "[core][datum][constexpr]") {
  constexpr std::array bad{height(VerticalDatum::mhhw, infinity)};
  STATIC_REQUIRE(table(VerticalDatum::msl, bad).error() ==
                 DatumTableError{NonFiniteHeight{VerticalDatum::mhhw}});
  constexpr std::array bad_negative{height(VerticalDatum::mllw, -infinity)};
  STATIC_REQUIRE(table(VerticalDatum::msl, bad_negative).error() ==
                 DatumTableError{NonFiniteHeight{VerticalDatum::mllw}});
}

TEST_CASE("offset(d, d) is zero for every datum, known or not",
          "[core][datum][constexpr]") {
  STATIC_REQUIRE(offset_is_zero_everywhere(DatumTable{}));
  STATIC_REQUIRE(
      offset_is_zero_everywhere(*table(VerticalDatum::msl, dyadic_heights)));
  STATIC_REQUIRE(DatumTable{}.offset(VerticalDatum::stnd,
                                     VerticalDatum::stnd) == metres(0.0));
}

TEST_CASE("offset is the difference of heights above MSL",
          "[core][datum][constexpr]") {
  constexpr DatumTable t = *table(VerticalDatum::msl, dyadic_heights);
  // from - to: the amount added to a series in `from` to express it in `to`.
  STATIC_REQUIRE(t.offset(VerticalDatum::mllw, VerticalDatum::mhhw) ==
                 metres(-1.5));
  STATIC_REQUIRE(t.offset(VerticalDatum::mhhw, VerticalDatum::mllw) ==
                 metres(1.5));
  STATIC_REQUIRE(t.offset(VerticalDatum::msl, VerticalDatum::mllw) ==
                 metres(0.75));
  STATIC_REQUIRE(t.offset(VerticalDatum::mllw, VerticalDatum::msl) ==
                 metres(-0.75));
  STATIC_REQUIRE(t.offset(VerticalDatum::ngvd29, VerticalDatum::navd88) ==
                 metres(-0.375));
}

TEST_CASE("offset reports the first missing datum",
          "[core][datum][constexpr]") {
  constexpr DatumTable t = *table(VerticalDatum::msl, dyadic_heights);
  STATIC_REQUIRE(t.offset(VerticalDatum::stnd, VerticalDatum::mllw).error() ==
                 MissingOffset{VerticalDatum::stnd});
  STATIC_REQUIRE(t.offset(VerticalDatum::mllw, VerticalDatum::stnd).error() ==
                 MissingOffset{VerticalDatum::stnd});
  // Both unknown: `from` is reported first.
  STATIC_REQUIRE(t.offset(VerticalDatum::stnd, VerticalDatum::igld85).error() ==
                 MissingOffset{VerticalDatum::stnd});
  STATIC_REQUIRE(t.offset(VerticalDatum::igld85, VerticalDatum::stnd).error() ==
                 MissingOffset{VerticalDatum::igld85});
  // MSL itself is always known.
  STATIC_REQUIRE(
      DatumTable{}.offset(VerticalDatum::msl, VerticalDatum::mllw).error() ==
      MissingOffset{VerticalDatum::mllw});
}

TEST_CASE("offsets are antisymmetric and transitive (dyadic, exact)",
          "[core][datum][constexpr]") {
  STATIC_REQUIRE(
      offsets_form_a_group(*table(VerticalDatum::msl, dyadic_heights)));
  STATIC_REQUIRE(offsets_form_a_group(*table(VerticalDatum::mllw, from_mllw)));
  STATIC_REQUIRE(offsets_form_a_group(DatumTable{}));
}
