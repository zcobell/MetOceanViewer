// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/core/station.hpp.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "mov/core/station.hpp"

using mov::core::DataSource;
using mov::core::FileStation;
using mov::core::GaugeStation;
using mov::core::parse_data_source;
using mov::core::Provider;
using mov::core::StationId;
using mov::core::StationIdError;
using mov::core::to_token;
using mov::core::provider::Coops;
using mov::core::provider::Ndbc;
using mov::core::provider::Usgs;
using mov::core::provider::Xtide;

namespace {

constexpr DataSource all_sources[] = {  // NOLINT(modernize-avoid-c-arrays)
    DataSource::noaa_coops, DataSource::usgs,   DataSource::ndbc,
    DataSource::xtide,      DataSource::adcirc, DataSource::dflowfm,
    DataSource::user};

constexpr bool tokens_round_trip() {
  return std::ranges::all_of(all_sources, [](DataSource s) {
    return parse_data_source(to_token(s)) == s;
  });
}

template <class T>
concept HasValue = requires(T&& t) { std::forward<T>(t).value(); };

/// make(raw) succeeds and holds exactly `expected`.
template <Provider P>
constexpr bool makes(std::string_view raw, std::string_view expected) {
  const auto id = StationId<P>::make(raw);
  return id.has_value() and id->value() == expected;
}

template <Provider P>
constexpr bool fails(std::string_view raw, StationIdError e) {
  return StationId<P>::make(raw).error_or(StationIdError{}) == e and
         not StationId<P>::make(raw).has_value();
}

/// Both parse and compare as `a < b` (and are unequal), or both are equal.
template <Provider P>
constexpr bool less(std::string_view a, std::string_view b) {
  const auto x = StationId<P>::make(a);
  const auto y = StationId<P>::make(b);
  return x and y and *x < *y and *x != *y;
}

template <Provider P>
constexpr bool same(std::string_view a, std::string_view b) {
  const auto x = StationId<P>::make(a);
  const auto y = StationId<P>::make(b);
  return x and y and *x == *y and not(*x < *y) and not(*y < *x);
}

constexpr std::string repeated(char c, std::size_t n) {
  std::string s;
  s.assign(n, c);
  return s;
}

}  // namespace

TEST_CASE("station types are values", "[core][station][constexpr]") {
  STATIC_REQUIRE(std::copyable<StationId<Coops>>);
  STATIC_REQUIRE(std::equality_comparable<StationId<Coops>>);
  STATIC_REQUIRE(std::totally_ordered<StationId<Coops>>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<StationId<Coops>>);
  // Parse, don't validate: no default and no way around make.
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<StationId<Coops>>);
  STATIC_REQUIRE_FALSE(std::is_constructible_v<StationId<Coops>, std::string>);
  STATIC_REQUIRE_FALSE(
      std::is_constructible_v<StationId<Coops>, std::string_view>);
  // A USGS id cannot be passed where a CO-OPS id is expected.
  STATIC_REQUIRE_FALSE(
      std::is_constructible_v<StationId<Coops>, StationId<Usgs>>);
  STATIC_REQUIRE_FALSE(std::is_convertible_v<StationId<Usgs>, StationId<Ndbc>>);
  // value() views the id, so a temporary id cannot hand it out.
  STATIC_REQUIRE(HasValue<const StationId<Coops>&>);
  STATIC_REQUIRE_FALSE(HasValue<StationId<Coops>>);

  STATIC_REQUIRE(std::copyable<GaugeStation<Usgs>>);
  STATIC_REQUIRE(std::equality_comparable<GaugeStation<Usgs>>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<GaugeStation<Usgs>>);
  STATIC_REQUIRE(std::copyable<FileStation>);
  STATIC_REQUIRE(std::equality_comparable<FileStation>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<FileStation>);
  // Location has no default, so neither do the station records.
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<FileStation>);
}

TEST_CASE("data source tokens round trip", "[core][station][constexpr]") {
  STATIC_REQUIRE(tokens_round_trip());
  STATIC_REQUIRE(to_token(DataSource::noaa_coops) == "noaa_coops");
  STATIC_REQUIRE(to_token(DataSource::dflowfm) == "dflowfm");
  STATIC_REQUIRE(to_token(DataSource::user) == "user");
  STATIC_REQUIRE_FALSE(parse_data_source("NOAA_COOPS").has_value());
  STATIC_REQUIRE_FALSE(parse_data_source(" usgs").has_value());
  STATIC_REQUIRE_FALSE(parse_data_source("").has_value());
  STATIC_REQUIRE_FALSE(parse_data_source("crms").has_value());
}

TEST_CASE("providers match their sources", "[core][station][constexpr]") {
  STATIC_REQUIRE(Coops::source == DataSource::noaa_coops);
  STATIC_REQUIRE(Usgs::source == DataSource::usgs);
  STATIC_REQUIRE(Ndbc::source == DataSource::ndbc);
  STATIC_REQUIRE(Xtide::source == DataSource::xtide);
}

TEST_CASE("CO-OPS ids are seven digits", "[core][station][constexpr]") {
  STATIC_REQUIRE(Coops::valid_id("8761724"));
  STATIC_REQUIRE_FALSE(Coops::valid_id("876172"));
  STATIC_REQUIRE_FALSE(Coops::valid_id("87617245"));
  STATIC_REQUIRE_FALSE(Coops::valid_id("876172a"));
  STATIC_REQUIRE_FALSE(Coops::valid_id(""));
  STATIC_REQUIRE(makes<Coops>(" 8761724\n", "8761724"));
  STATIC_REQUIRE(fails<Coops>("8761 724", StationIdError::invalid));
}

TEST_CASE("USGS ids are agency-number", "[core][station][constexpr]") {
  STATIC_REQUIRE(Usgs::valid_id("USGS-07374000"));
  STATIC_REQUIRE(Usgs::valid_id("a-1"));
  STATIC_REQUIRE_FALSE(Usgs::valid_id("07374000"));
  STATIC_REQUIRE_FALSE(Usgs::valid_id("USGS-"));
  STATIC_REQUIRE_FALSE(Usgs::valid_id("-07374000"));
  STATIC_REQUIRE_FALSE(Usgs::valid_id("USGS-0737-4000"));
  STATIC_REQUIRE_FALSE(Usgs::valid_id("USGS 07374000"));
  STATIC_REQUIRE_FALSE(Usgs::valid_id("USGS-0737_4000"));
  // No case folding: the agency code is kept as given.
  STATIC_REQUIRE(makes<Usgs>("\tusgs-07374000 ", "usgs-07374000"));
}

TEST_CASE("NDBC ids are five characters, upper-cased",
          "[core][station][constexpr]") {
  STATIC_REQUIRE(Ndbc::valid_id("42001"));
  STATIC_REQUIRE(Ndbc::valid_id("BURL1"));
  // The predicate sees the upper-cased text; make does the folding.
  STATIC_REQUIRE_FALSE(Ndbc::valid_id("burl1"));
  STATIC_REQUIRE_FALSE(Ndbc::valid_id("4200"));
  STATIC_REQUIRE_FALSE(Ndbc::valid_id("420011"));
  STATIC_REQUIRE_FALSE(Ndbc::valid_id("42-01"));
  STATIC_REQUIRE(makes<Ndbc>(" burl1 ", "BURL1"));
  STATIC_REQUIRE(fails<Ndbc>("burl_", StationIdError::invalid));
}

TEST_CASE("XTide ids are station names", "[core][station][constexpr]") {
  STATIC_REQUIRE(Xtide::valid_id("Battery, New York Harbor, New York"));
  STATIC_REQUIRE(Xtide::valid_id("\xC3\x89le"));  // UTF-8 bytes are fine
  STATIC_REQUIRE_FALSE(Xtide::valid_id(""));
  STATIC_REQUIRE_FALSE(Xtide::valid_id("a\tb"));
  STATIC_REQUIRE_FALSE(Xtide::valid_id("a\x7F"));
  STATIC_REQUIRE_FALSE(Xtide::valid_id(std::string_view{"a\0b", 3}));
  STATIC_REQUIRE(Xtide::valid_id(repeated('x', 255)));
  STATIC_REQUIRE_FALSE(Xtide::valid_id(repeated('x', 256)));
  STATIC_REQUIRE(makes<Xtide>("  Key West, Florida \r\n", "Key West, Florida"));
}

TEST_CASE("blank ids are empty, not invalid", "[core][station][constexpr]") {
  STATIC_REQUIRE(fails<Coops>("", StationIdError::empty));
  STATIC_REQUIRE(fails<Usgs>(" \t\r\n", StationIdError::empty));
  STATIC_REQUIRE(fails<Xtide>("   ", StationIdError::empty));
}

TEST_CASE("station ids are ordered by their text",
          "[core][station][constexpr]") {
  STATIC_REQUIRE(less<Coops>("8761724", "8761725"));
  STATIC_REQUIRE(same<Coops>("8761724", " 8761724"));
  STATIC_REQUIRE(same<Ndbc>("burl1", "BURL1"));
}
