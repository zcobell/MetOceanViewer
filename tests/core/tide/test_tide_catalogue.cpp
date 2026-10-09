// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Run-time tests of the whole catalogue (its invariants, NOAA's published
// speeds) and the exhaustive constituent-name grammar. constituent.cpp
// static_asserts the same invariants; here they run for coverage and so that
// a failure names the entry.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

#include "mov/core/tide/catalogue.hpp"
#include "mov/core/tide/constituent.hpp"
#include "noaa_speeds.hpp"

using mov::core::tide::catalogue_aliases;
using mov::core::tide::ConstituentName;
using mov::core::tide::ConstituentNameError;
using mov::core::tide::nos_catalogue;
using mov::core::tide::nos_standard_count;
using mov::test::tide::noaa_speeds;

namespace detail = mov::core::detail;
namespace tide = mov::core::tide;

TEST_CASE("the catalogue holds its invariants", "[core][tide][catalogue]") {
  CHECK(nos_catalogue.size() == 120);
  CHECK(detail::names_unique(nos_catalogue, catalogue_aliases));
  CHECK(detail::compound_terms_resolve(nos_catalogue));
  CHECK(detail::species_match_names(nos_catalogue));
  CHECK(detail::notes_plain(nos_catalogue));
  CHECK(detail::aliases_resolve(nos_catalogue, catalogue_aliases));
  for (const auto& e : nos_catalogue) {
    INFO(e.name.view());
    CHECK(tide::flatten(e.definition, detail::lookup_in(nos_catalogue))
              .has_value());
  }
}

TEST_CASE("speeds match NOAA's published ones", "[core][tide][catalogue]") {
  // NOAA rounds speeds to 7-8 significant digits (HJ 7.1). For the NOS 37
  // the format's checksum tolerance, 1e-5 deg/h, holds (largest: M6,
  // 7.4e-6). The higher compounds of 9455920 are rounded to fewer decimals:
  // 3MNK9 differs by 1.08e-5, so they get 2e-5. The congen golden table
  // checks the definitions themselves.
  REQUIRE(noaa_speeds.size() == nos_catalogue.size());
  for (std::size_t i = 0; i < noaa_speeds.size(); ++i) {
    const auto& published = noaa_speeds[i];
    INFO(published.name);
    CHECK(nos_catalogue[i].name.view() == published.name);
    const auto a = tide::flatten(nos_catalogue[i].definition,
                                 detail::lookup_in(nos_catalogue));
    REQUIRE(a.has_value());
    const double tolerance = i < nos_standard_count ? 1e-5 : 2e-5;
    CHECK(std::abs(a->speed_deg_per_hour() - published.speed) <= tolerance);
  }
}

TEST_CASE("constituent names: every byte at the first two positions",
          "[core][tide][catalogue]") {
  // The grammar ^[A-Za-z0-9][A-Za-z0-9()_.+-]{0,31}$, restated plainly.
  const auto alnum = [](char c) {
    return (c >= 'A' and c <= 'Z') or (c >= 'a' and c <= 'z') or
           (c >= '0' and c <= '9');
  };
  const std::string_view punctuation{"()_.+-"};
  for (int b = 0; b < 256; ++b) {
    const char c = static_cast<char>(b);
    INFO("byte " << b);
    const auto first = ConstituentName::make(std::string{c, 'A'});
    CHECK(first.has_value() == alnum(c));
    if (not first) {
      CHECK(first.error() == ConstituentNameError::bad_char);
    }
    const std::string text{'A', c};
    const auto second = ConstituentName::make(text);
    const bool allowed =
        alnum(c) or punctuation.find(c) != std::string_view::npos;
    CHECK(second.has_value() == allowed);
    if (second) {
      CHECK(second->view() == text);
    }
  }
}

TEST_CASE("constituent names: 32 bytes at most", "[core][tide][catalogue]") {
  const std::string longest(32, 'A');
  const auto ok = ConstituentName::make(longest);
  REQUIRE(ok.has_value());
  CHECK(ok->view() == longest);
  CHECK(ConstituentName::make(std::string(33, 'A')).error() ==
        ConstituentNameError::too_long);
  CHECK(ConstituentName::make("").error() == ConstituentNameError::empty);
  // Too long is reported before a bad character.
  CHECK(ConstituentName::make(std::string(33, '-')).error() ==
        ConstituentNameError::too_long);
}
