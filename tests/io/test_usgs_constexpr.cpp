// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for the USGS wire layer: the endpoint paths, the map
// parameters and the link refusals' tokens.

#include <catch2/catch_test_macros.hpp>

#include "mov/core/provider/usgs.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/io/wire/usgs.hpp"

namespace {

using mov::io::wire::LinkError;
using mov::io::wire::to_token;
using mov::io::wire::usgs::Endpoint;
using mov::io::wire::usgs::endpoint_of;
using mov::io::wire::usgs::items_path;

}  // namespace

TEST_CASE("USGS endpoints and the map parameters", "[io][usgs][constexpr]") {
  STATIC_REQUIRE(items_path(Endpoint::continuous) ==
                 "/ogcapi/v1/collections/continuous/items");
  STATIC_REQUIRE(items_path(Endpoint::daily) ==
                 "/ogcapi/v1/collections/daily/items");
  STATIC_REQUIRE(items_path(Endpoint::combined_metadata) ==
                 "/ogcapi/v1/collections/combined-metadata/items");
  STATIC_REQUIRE(endpoint_of(mov::core::usgs::Collection::continuous) ==
                 Endpoint::continuous);
  STATIC_REQUIRE(endpoint_of(mov::core::usgs::Collection::daily) ==
                 Endpoint::daily);
  STATIC_REQUIRE(mov::io::wire::usgs::page_rows == 50000);
  // Decision 33: every map parameter is a parameter code.
  STATIC_REQUIRE(mov::io::wire::usgs::map_parameters.size() == 4);
  STATIC_REQUIRE(mov::core::usgs::ParameterCode::make(
                     mov::io::wire::usgs::map_parameters[2])
                     .has_value());
}

TEST_CASE("a refused link names its reason", "[io][usgs][constexpr]") {
  STATIC_REQUIRE(to_token(LinkError::too_long) == "too_long");
  STATIC_REQUIRE(to_token(LinkError::bad_character) == "bad_character");
  STATIC_REQUIRE(to_token(LinkError::other_origin) == "other_origin");
  STATIC_REQUIRE(to_token(LinkError::other_path) == "other_path");
}
