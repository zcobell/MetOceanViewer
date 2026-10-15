// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The ConstituentSet over the whole built-in catalogue, the per-year node
// factor cache and the YearArguments table (WP H2).

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "harmonics_example.hpp"
#include "mov/core/tide/astronomy.hpp"
#include "mov/core/tide/builtin.hpp"
#include "mov/core/tide/catalogue.hpp"
#include "mov/core/tide/constituent.hpp"
#include "mov/core/tide/constituent_set.hpp"
#include "mov/core/tide/node_factor.hpp"
#include "mov/core/tide/year_arguments.hpp"

namespace t = mov::core::tide;
namespace chr = std::chrono;
using mov::test::tide::must;

namespace {

t::ConstituentSet catalogue_set() {
  std::vector<t::DictionaryEntry> entries;
  entries.reserve(t::nos_catalogue.size());
  for (const t::CatalogueEntry& e : t::nos_catalogue) {
    entries.push_back(t::DictionaryEntry{
        .name = e.name,
        .definition = e.definition,
        .description =
            e.note ? std::optional<std::string>{*e.note} : std::nullopt});
  }
  return t::ConstituentSet::make(std::move(entries)).value();
}

t::YearAstronomy astronomy(int y) {
  return t::YearAstronomy::make(chr::year{y}).value();
}

/// |a - b| reduced to [0, 180].
double angle_between(double a, double b) {
  const double d = std::fmod(std::abs(a - b), 360.0);
  return d > 180.0 ? 360.0 - d : d;
}

}  // namespace

TEST_CASE("the catalogue as a constituent set",
          "[core][tide][constituent_set]") {
  const t::ConstituentSet set = catalogue_set();
  REQUIRE(set.size() == t::nos_catalogue.size());
  std::optional<std::tuple<std::int32_t, double, std::string_view>> previous;
  for (const t::ConstituentIndex i : set.indices()) {
    const std::string_view name = set.name(i).view();
    INFO(name);
    CHECK(set.find(name) == i);
    CHECK(set.argument(i) == t::catalogue_argument(name));
    CHECK(set.speed(i) == set.argument(i).speed_deg_per_hour());
    CHECK(set.definition(i) ==
          t::nos_catalogue[must(t::catalogue_index(name))].definition);
    CHECK(set.description(i).has_value() ==
          t::catalogue_note(name).has_value());
    const std::tuple key{set.argument(i).species(), set.speed(i), name};
    CHECK((not previous or *previous < key));
    previous = key;
  }
  CHECK_FALSE(set.find("m2"));
  CHECK_FALSE(set.find("LDA2"));  // aliases are the builder's, not the set's
}

TEST_CASE("a year's node factors are each formula's value, evaluated once",
          "[core][tide][year_arguments]") {
  for (const int y : {1700, 1900, 2026, 2200}) {
    INFO(y);
    const t::YearAstronomy a = astronomy(y);
    for (const t::NodeFactor f : t::all_node_factors) {
      CHECK(a.node_factors()[f] == t::node_factor(f, a.at_mid_year()));
    }
    CHECK(a.node_factors() == t::NodeFactorValues::of(a.at_mid_year()));
  }
}

TEST_CASE("cached node factors give the same bits as per-constituent ones",
          "[core][tide][year_arguments]") {
  for (int y = 1700; y <= 2200; y += 25) {
    INFO(y);
    const t::YearAstronomy a = astronomy(y);
    for (const t::CatalogueEntry& e : t::nos_catalogue) {
      const t::Argument arg = must(t::catalogue_argument(e.name.view()));
      CHECK(std::bit_cast<std::uint64_t>(arg.f(a.node_factors())) ==
            std::bit_cast<std::uint64_t>(arg.f(a.at_mid_year())));
    }
  }
}

TEST_CASE("YearArguments tabulates equilibrium for every constituent",
          "[core][tide][year_arguments]") {
  const t::ConstituentSet set = catalogue_set();
  for (const int y : {1700, 1969, 2026, 2200}) {
    INFO(y);
    const auto table = t::year_arguments(set, chr::year{y});
    REQUIRE(table.has_value());
    CHECK(table->year() == chr::year{y});
    CHECK(table->start() == t::year_start(chr::year{y}));
    REQUIRE(table->size() == set.size());
    const t::YearAstronomy a = astronomy(y);
    CHECK(*table == t::year_arguments(set, a));
    for (const t::ConstituentIndex i : set.indices()) {
      const t::Equilibrium e = (*table)[i];
      CHECK(e == t::equilibrium(set.argument(i), a));
      CHECK(e.node_factor == set.argument(i).f(a.at_mid_year()));
    }
  }
  CHECK(t::year_arguments(set, chr::year{1699}).error() ==
        t::YearOutOfRange{.year = chr::year{1699}});
  CHECK(t::year_arguments(set, chr::year{2201}).error() ==
        t::YearOutOfRange{.year = chr::year{2201}});
}

TEST_CASE("the worked example's 2026 arguments (docs/harmonics-json.md 13.2)",
          "[core][tide][year_arguments]") {
  std::vector<t::DictionaryEntry> entries;
  for (auto& c : mov::test::tide::example_constituents()) {
    entries.push_back(std::move(c.entry));
  }
  const t::ConstituentSet set =
      t::ConstituentSet::make(std::move(entries)).value();
  const auto table = t::year_arguments(set, chr::year{2026}).value();
  struct Vector {
    std::string_view name;
    double speed;
    double argument;
    double f;
  };
  // HJ 13.2's table (4 decimals for the argument, 5 for f).
  const std::vector<Vector> vectors{
      {.name = "M2", .speed = 28.9841042, .argument = 66.3564, .f = 0.96735},
      {.name = "S2", .speed = 30.0, .argument = 0.0, .f = 1.0},
      {.name = "N2", .speed = 28.4397295, .argument = 59.6632, .f = 0.96735},
      {.name = "K1", .speed = 15.0410686, .argument = 14.2609, .f = 1.10312},
      {.name = "O1", .speed = 13.9430356, .argument = 50.6487, .f = 1.16682},
      {.name = "M4", .speed = 57.9682084, .argument = 132.7128, .f = 0.93577},
      {.name = "MS4", .speed = 58.9841042, .argument = 66.3564, .f = 0.96735},
  };
  for (const Vector& v : vectors) {
    INFO(v.name);
    const t::ConstituentIndex i = must(set.find(v.name));
    CHECK(std::abs(set.speed(i) - v.speed) < 1e-7);
    CHECK(angle_between(table[i].argument.degrees(), v.argument) < 5e-5);
    CHECK(std::abs(table[i].node_factor - v.f) < 5e-6);
  }
}

TEST_CASE("builtin_argument looks up a name or an alias",
          "[core][tide][catalogue]") {
  CHECK(t::builtin_argument("M2") == t::catalogue_argument("M2"));
  CHECK(t::builtin_argument("LDA2") == t::catalogue_argument("LAM2"));
  CHECK_FALSE(t::builtin_argument("XYZ9"));
  CHECK_FALSE(t::builtin_argument("m2"));
}
