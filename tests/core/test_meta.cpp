// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <optional>
#include <string>
#include <utility>

#include "mov/core/datum.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/units.hpp"

using mov::core::AssumeDatumError;
using mov::core::AssumeUnitError;
using mov::core::GenericQuantity;
using mov::core::LengthUnit;
using mov::core::parse_unit;
using mov::core::Quantity;
using mov::core::QuantityId;
using mov::core::SeriesMeta;
using mov::core::SpeedUnit;
using mov::core::Unit;
using mov::core::VerticalDatum;

namespace {

const SeriesMeta level = SeriesMeta::make({.quantity = Quantity::water_level,
                                           .label = "old",
                                           .unit = LengthUnit::meter});

}  // namespace

TEST_CASE("a default SeriesMeta is the unknown generic series",
          "[core][meta]") {
  const SeriesMeta meta;
  CHECK(meta.quantity() == QuantityId{GenericQuantity::value()});
  CHECK(meta.label().empty());
  CHECK(not meta.unit().has_value());
  CHECK(not meta.datum().has_value());
  CHECK(SeriesMeta::make({}) == SeriesMeta{});
}

TEST_CASE("SeriesMeta::make is total and keeps every field", "[core][meta]") {
  const SeriesMeta meta = SeriesMeta::make({.quantity = Quantity::wind_speed,
                                            .label = "Wind at Pilots",
                                            .unit = SpeedUnit::knot});
  CHECK(meta.quantity() == QuantityId{Quantity::wind_speed});
  CHECK(meta.label() == "Wind at Pilots");
  CHECK(meta.unit() == std::optional<Unit>{SpeedUnit::knot});
  // make cannot set a datum: assume_datum is the only way.
  CHECK(not meta.datum().has_value());
}

TEST_CASE("a datum is accepted exactly where datum_applicable holds",
          "[core][meta]") {
  const auto mllw = level.assume_datum(VerticalDatum::mllw);
  REQUIRE(mllw.has_value());
  CHECK(mllw->datum() == VerticalDatum::mllw);
  CHECK(SeriesMeta::make({.quantity = Quantity::water_level_prediction})
            .assume_datum(VerticalDatum::mhhw)
            .has_value());
  // Legacy files put a datum on the generic `value` quantity (SN section 11).
  CHECK(SeriesMeta{}.assume_datum(VerticalDatum::stnd).has_value());

  CHECK(SeriesMeta::make({.quantity = Quantity::wind_speed})
            .assume_datum(VerticalDatum::msl) ==
        std::unexpected{AssumeDatumError::not_applicable});
  CHECK(SeriesMeta::make({.quantity = Quantity::current_u})
            .assume_datum(VerticalDatum::mllw) ==
        std::unexpected{AssumeDatumError::not_applicable});
}

TEST_CASE("assume_datum refuses to replace a datum", "[core][meta]") {
  const auto mllw = level.assume_datum(VerticalDatum::mllw);
  REQUIRE(mllw.has_value());
  CHECK(mllw->assume_datum(VerticalDatum::mllw) ==
        std::unexpected{AssumeDatumError::already_set});
  CHECK(mllw->assume_datum(VerticalDatum::navd88) ==
        std::unexpected{AssumeDatumError::already_set});
}

TEST_CASE("with_label is total and changes only the label", "[core][meta]") {
  const auto with_datum = level.assume_datum(VerticalDatum::msl);
  REQUIRE(with_datum.has_value());
  const SeriesMeta& meta = *with_datum;
  const SeriesMeta relabelled = meta.with_label("new");
  CHECK(relabelled.label() == "new");
  CHECK(relabelled.quantity() == meta.quantity());
  CHECK(relabelled.unit() == meta.unit());
  CHECK(relabelled.datum() == meta.datum());
  CHECK(meta.label() == "old");  // the source is unchanged

  SeriesMeta moved_from = meta;
  const SeriesMeta from_rvalue = std::move(moved_from).with_label("");
  CHECK(from_rvalue.label().empty());
  CHECK(from_rvalue.datum() == VerticalDatum::msl);
}

TEST_CASE("assume_unit fills an unset unit and refuses to replace one",
          "[core][meta]") {
  const SeriesMeta unknown;
  const auto feet = unknown.assume_unit(LengthUnit::foot);
  REQUIRE(feet.has_value());
  CHECK(feet->unit() == std::optional<Unit>{LengthUnit::foot});

  // An engaged unit changes only through convert, even to itself.
  CHECK(feet->assume_unit(LengthUnit::meter) ==
        std::unexpected{AssumeUnitError::already_set});
  CHECK(feet->assume_unit(LengthUnit::foot) ==
        std::unexpected{AssumeUnitError::already_set});

  const auto percent = parse_unit("%");
  REQUIRE(percent.has_value());
  const auto with_percent = unknown.assume_unit(percent.value_or(Unit{}));
  REQUIRE(with_percent.has_value());
  CHECK(with_percent->unit() == percent);
}

TEST_CASE("SeriesMeta equality compares every field", "[core][meta]") {
  CHECK(level == level.with_label("old"));
  CHECK(level != level.with_label("b"));
  CHECK(level != SeriesMeta::make({.quantity = Quantity::water_level_prediction,
                                   .label = "old",
                                   .unit = LengthUnit::meter}));
  CHECK(level !=
        SeriesMeta::make({.quantity = Quantity::water_level, .label = "old"}));
  const auto with_datum = level.assume_datum(VerticalDatum::msl);
  REQUIRE(with_datum.has_value());
  CHECK(level != *with_datum);
}
