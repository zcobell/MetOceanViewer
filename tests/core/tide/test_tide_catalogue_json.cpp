// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// docs/schemas/constituents-nos.json is generated here from the core
// catalogue (docs/harmonics-engine.md section 2.3), and the schema's
// node-factor enum is drift-tested against the core enum. To rewrite the
// committed file after a catalogue change, run this test with
// MOV_UPDATE_CONSTITUENTS_JSON=1.

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/tide/catalogue.hpp"
#include "mov/core/tide/constituent.hpp"
#include "mov/core/tide/datums.hpp"
#include "mov/core/tide/node_factor.hpp"
#include "mov/core/tide/station_info.hpp"
#include "mov/test/env.hpp"
#include "mov/test/fixture.hpp"
#include "mov/test/scratch_dir.hpp"

using mov::core::tide::all_node_factors;
using mov::core::tide::Argument;
using mov::core::tide::catalogue_argument;
using mov::core::tide::catalogue_note;
using mov::core::tide::CatalogueEntry;
using mov::core::tide::CompoundTerms;
using mov::core::tide::ConstituentName;
using mov::core::tide::Darwin;
using mov::core::tide::nos_catalogue;

namespace tide = mov::core::tide;

namespace {

std::filesystem::path schemas_dir() {
  return mov::test::fixture("").parent_path().parent_path().parent_path() /
         "docs" / "schemas";
}

/// The flattened Argument of a catalogue name; an unknown name fails the
/// test.
Argument argument_of(std::string_view name) {
  auto a = catalogue_argument(name);
  if (not a) {
    FAIL("not a catalogue name: " << name);
  }
  return *std::move(a);
}

std::string join(const auto& values) {
  std::string out;
  for (const auto& v : values) {
    out += out.empty() ? std::format("{}", v) : std::format(", {}", v);
  }
  return out;
}

std::string definition_json(const CatalogueEntry& e) {
  if (std::holds_alternative<Darwin>(e.definition)) {
    const auto& d = std::get<Darwin>(e.definition);
    std::vector<int> v(d.v.begin(), d.v.end());
    v.push_back(d.c_deg);
    const std::vector<int> u(d.u.begin(), d.u.end());
    return std::format(R"("darwin": {{"V": [{}], "u": [{}], "f": "{}"}})",
                       join(v), join(u), tide::to_token(d.f));
  }
  std::string terms;
  for (const auto& t : std::get<CompoundTerms>(e.definition).terms()) {
    terms += std::format(R"({}"{}": {})", terms.empty() ? "" : ", ",
                         t.base.view(), static_cast<int>(t.coefficient));
  }
  return std::format(R"("compound": {{{}}})", terms);
}

/// The dictionary in file syntax (docs/harmonics-json.md section 7), sorted
/// by (species, speed, name) and one entry per line as section 3 asks.
std::string catalogue_json() {
  struct Row {
    std::int32_t species;
    double speed;
    std::string_view name;
    const CatalogueEntry* entry;
  };
  std::vector<Row> rows;
  for (const CatalogueEntry& e : nos_catalogue) {
    const Argument a = argument_of(e.name.view());
    rows.push_back(Row{.species = a.species(),
                       .speed = a.speed_deg_per_hour(),
                       .name = e.name.view(),
                       .entry = &e});
  }
  std::ranges::sort(rows, {}, [](const Row& r) {
    return std::tuple{r.species, r.speed, r.name};
  });
  std::string out =
      "{\n"
      R"(  "$comment": "Generated from mov::core::tide::nos_catalogue )"
      R"((src/core/include/mov/core/tide/catalogue.hpp) by )"
      R"(tests/core/tide/test_tide_catalogue_json.cpp; do not edit. )"
      R"(Definitions: SP 98 Table 2 as libcongen 1.7 codes them, in NOAA )"
      R"(CO-OPS spelling (docs/harmonics-json.md Appendix B).",)"
      "\n  \"constituents\": [\n";
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const Row& r = rows[i];
    const auto note = catalogue_note(r.name);
    out += std::format(
        R"(    {{"name": "{}", "speed": {}, {}{}}}{})", r.name, r.speed,
        definition_json(*r.entry),
        note ? std::format(R"(, "description": "{}")", *note) : std::string{},
        i + 1 < rows.size() ? "," : "");
    out += '\n';
  }
  out += "  ]\n}\n";
  return out;
}

bool in_range(const nlohmann::json& v, int low, int high) {
  return v.is_number_integer() and v.get<int>() >= low and v.get<int>() <= high;
}

}  // namespace

TEST_CASE("constituents-nos.json is current", "[core][tide][drift]") {
  const auto path = schemas_dir() / "constituents-nos.json";
  const std::string generated = catalogue_json();
  if (mov::test::env("MOV_UPDATE_CONSTITUENTS_JSON")) {
    std::ofstream{path, std::ios::binary} << generated;
    // Fail even so: a run that rewrites the file is never green, so no run
    // can pass by regenerating it.
    FAIL("rewrote " << path.string()
                    << "; rerun without MOV_UPDATE_CONSTITUENTS_JSON");
  }
  INFO("rerun with MOV_UPDATE_CONSTITUENTS_JSON=1 to rewrite "
       << path.string());
  CHECK(mov::test::read_bytes(path) == generated);
}

TEST_CASE("the schema's node_factor enum is the core NodeFactor",
          "[core][tide][drift]") {
  const auto schema = nlohmann::json::parse(
      mov::test::read_bytes(schemas_dir() / "harmonics-v1.schema.json"));
  std::vector<std::string> tokens;
  for (const auto& t : schema.at("$defs").at("node_factor").at("enum")) {
    tokens.push_back(t.get<std::string>());
  }
  std::vector<std::string> core;
  core.reserve(all_node_factors.size());
  for (const auto f : all_node_factors) {
    core.emplace_back(tide::to_token(f));
  }
  CHECK(tokens == core);
}

TEST_CASE("the generated dictionary is a valid format dictionary",
          "[core][tide][drift]") {
  // The rules of docs/harmonics-json.md 7.1 and the schema's bounds,
  // checked on the generated text as a reader would see it.
  const auto doc = nlohmann::json::parse(catalogue_json());
  const auto& entries = doc.at("constituents");
  REQUIRE(entries.size() == nos_catalogue.size());
  std::vector<std::string> darwin_names;
  for (const auto& e : entries) {
    if (e.contains("darwin")) {
      darwin_names.push_back(e.at("name").get<std::string>());
    }
  }
  std::optional<std::tuple<int, double, std::string>> previous;
  for (const auto& e : entries) {
    const auto name = e.at("name").get<std::string>();
    INFO(name);
    CHECK(ConstituentName::make(name).has_value());
    const double speed = e.at("speed").get<double>();
    const Argument a = argument_of(name);
    CHECK(speed == a.speed_deg_per_hour());  // shortest round trip is exact
    const std::tuple key{a.species(), speed, name};
    CHECK((not previous or *previous < key));
    previous = key;
    CHECK(e.contains("description") == catalogue_note(name).has_value());
    CHECK(e.contains("darwin") != e.contains("compound"));
    if (e.contains("darwin")) {
      const auto& d = e.at("darwin");
      const auto& v = d.at("V");
      REQUIRE(v.size() == 6);
      CHECK(in_range(v[0], 0, 16));
      for (std::size_t i = 1; i < 5; ++i) {
        CHECK(in_range(v[i], -64, 64));
      }
      CHECK(in_range(v[5], -360, 360));
      REQUIRE(d.at("u").size() == 7);
      for (const auto& u : d.at("u")) {
        CHECK(in_range(u, -64, 64));
      }
      CHECK(tide::parse_node_factor(d.at("f").get<std::string>()).has_value());
    } else {
      const auto& c = e.at("compound");
      CHECK(not c.empty());
      CHECK(c.size() <= 8);
      for (const auto& [base, coefficient] : c.items()) {
        CHECK(in_range(coefficient, -16, 16));
        CHECK(coefficient.get<int>() != 0);
        CHECK(std::ranges::find(darwin_names, base) != darwin_names.end());
      }
    }
  }
}

namespace {

/// The tokens of the schema's enum at JSON pointer `where`, in order.
std::vector<std::string> schema_enum(const std::string& where) {
  const auto schema = nlohmann::json::parse(
      mov::test::read_bytes(schemas_dir() / "harmonics-v1.schema.json"));
  std::vector<std::string> tokens;
  for (const auto& t : schema.at(nlohmann::json::json_pointer{where})) {
    tokens.push_back(t.get<std::string>());
  }
  return tokens;
}

template <class Enum, std::size_t N>
std::vector<std::string> core_tokens(const std::array<Enum, N>& values) {
  std::vector<std::string> tokens;
  tokens.reserve(N);
  for (const Enum v : values) {
    tokens.emplace_back(tide::to_token(v));
  }
  return tokens;
}

}  // namespace

TEST_CASE("the schema's datum and analysis enums are the core enums",
          "[core][tide][drift]") {
  CHECK(schema_enum("/$defs/datum_token/enum") ==
        core_tokens(tide::all_datum_tokens));
  CHECK(schema_enum("/$defs/computed_datum_token/enum") ==
        core_tokens(tide::computable_datum_tokens));
  CHECK(schema_enum("/$defs/station/properties/analysis/enum") ==
        core_tokens(tide::all_analyses));
}
