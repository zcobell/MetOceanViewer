// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The enums of docs/schemas/station-catalog-v1.schema.json marked
// "drift-tested" against the tokens the reader and writer use (tests may
// include nlohmann/json; src may not outside src/io/json/).

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/provider/coops.hpp"
#include "mov/io/detail/station_catalog_text.hpp"
#include "mov/test/scratch_dir.hpp"

namespace {

namespace tokens = mov::io::detail::catalog;

const nlohmann::json& schema() {
  static const nlohmann::json loaded = nlohmann::json::parse(
      mov::test::read_bytes(std::filesystem::path{MOV_TEST_REPO_DIR} / "docs" /
                            "schemas" / "station-catalog-v1.schema.json"));
  return loaded;
}

std::vector<std::string> enum_of(std::string_view def) {
  const auto& values = schema().at("$defs").at(std::string{def}).at("enum");
  std::vector<std::string> out;
  out.reserve(values.size());
  for (const auto& v : values) {
    out.push_back(v.get<std::string>());
  }
  return out;
}

template <class Rows>
std::vector<std::string> tokens_of(const Rows& rows) {
  std::vector<std::string> out;
  out.reserve(rows.size());
  for (const auto& row : rows) {
    out.emplace_back(row.token);
  }
  return out;
}

}  // namespace

TEST_CASE("the schema's enums are the reader's tokens, in enumerator order",
          "[io][catalog][schema]") {
  CHECK(enum_of("observed") == tokens_of(tokens::observed_tokens));
  CHECK(enum_of("met") == tokens_of(tokens::met_tokens));
  CHECK(enum_of("predictions") == tokens_of(tokens::predictions_tokens));

  std::vector<std::string> datums;
  datums.reserve(mov::core::coops::every_datum.size());
  for (const auto d : mov::core::coops::every_datum) {
    datums.emplace_back(tokens::datum_token(d));
  }
  CHECK(enum_of("coops_datum") == datums);

  std::vector<std::string> heights;
  for (const auto& [name, value] : schema()
                                       .at("$defs")
                                       .at("coops_block")
                                       .at("properties")
                                       .at("datums")
                                       .at("properties")
                                       .items()) {
    heights.push_back(name);
  }
  std::vector<std::string> core_tokens;
  core_tokens.reserve(
      mov::core::Enumerators<mov::core::VerticalDatum>::values.size());
  for (const auto d :
       mov::core::Enumerators<mov::core::VerticalDatum>::values) {
    core_tokens.emplace_back(mov::core::to_string(d));
  }
  std::ranges::sort(heights);
  std::ranges::sort(core_tokens);
  CHECK(heights == core_tokens);
}

TEST_CASE("the schema names the format and accepts every 1.x version",
          "[io][catalog][schema]") {
  CHECK(schema().at("properties").at("format").at("const") ==
        "metoceanviewer-stations");
  CHECK(schema().at("properties").at("version").at("pattern") ==
        "^1\\.(0|[1-9][0-9]{0,8})$");
}
