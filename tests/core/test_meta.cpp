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

using mov::core::GenericQuantity;
using mov::core::LengthUnit;
using mov::core::MetaError;
using mov::core::parse_unit;
using mov::core::Quantity;
using mov::core::QuantityId;
using mov::core::SeriesMeta;
using mov::core::SpeedUnit;
using mov::core::Unit;
using mov::core::VerticalDatum;

namespace {

SeriesMeta meta_or_default(const std::expected<SeriesMeta, MetaError>& m) {
  REQUIRE(m.has_value());
  return m.value_or(SeriesMeta{});
}

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

TEST_CASE("SeriesMeta::make keeps every field", "[core][meta]") {
  const auto meta = SeriesMeta::make({.quantity = Quantity::water_level,
                                      .label = "Water level at Pilots",
                                      .unit = LengthUnit::foot,
                                      .datum = VerticalDatum::navd88});
  REQUIRE(meta.has_value());
  CHECK(meta->quantity() == QuantityId{Quantity::water_level});
  CHECK(meta->label() == "Water level at Pilots");
  CHECK(meta->unit() == std::optional<Unit>{LengthUnit::foot});
  CHECK(meta->datum() == VerticalDatum::navd88);
}

TEST_CASE("a datum on a quantity that cannot carry one is rejected (C3)",
          "[core][meta]") {
  CHECK(SeriesMeta::make({.quantity = Quantity::wind_speed,
                          .unit = SpeedUnit::knot,
                          .datum = VerticalDatum::msl}) ==
        std::unexpected{MetaError::datum_not_applicable});
  CHECK(SeriesMeta::make(
            {.quantity = Quantity::current_u, .datum = VerticalDatum::mllw})
            .error_or(MetaError::already_set) ==
        MetaError::datum_not_applicable);
  // The same quantity without a datum is fine.
  CHECK(SeriesMeta::make({.quantity = Quantity::wind_speed}).has_value());
}

TEST_CASE("a datum is accepted exactly where datum_applicable holds",
          "[core][meta]") {
  CHECK(SeriesMeta::make(
            {.quantity = Quantity::water_level, .datum = VerticalDatum::mllw})
            .has_value());
  CHECK(SeriesMeta::make({.quantity = Quantity::water_level_prediction,
                          .datum = VerticalDatum::mhhw})
            .has_value());
  // Legacy files put a datum on the generic `value` quantity (SN section 11).
  CHECK(SeriesMeta::make({.datum = VerticalDatum::stnd}).has_value());
}

TEST_CASE("with_label is total and changes only the label", "[core][meta]") {
  const SeriesMeta meta =
      meta_or_default(SeriesMeta::make({.quantity = Quantity::water_level,
                                        .label = "old",
                                        .unit = LengthUnit::meter,
                                        .datum = VerticalDatum::msl}));
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

  // An engaged unit changes only through convert (WP3), even to itself.
  CHECK(feet->assume_unit(LengthUnit::meter) ==
        std::unexpected{MetaError::already_set});
  CHECK(feet->assume_unit(LengthUnit::foot) ==
        std::unexpected{MetaError::already_set});

  const auto percent = parse_unit("%");
  REQUIRE(percent.has_value());
  const auto with_percent = unknown.assume_unit(percent.value_or(Unit{}));
  REQUIRE(with_percent.has_value());
  CHECK(with_percent->unit() == percent);
}

TEST_CASE("assume_datum fills an unset datum where it applies",
          "[core][meta]") {
  const SeriesMeta level =
      meta_or_default(SeriesMeta::make({.quantity = Quantity::water_level}));
  const auto mllw = level.assume_datum(VerticalDatum::mllw);
  REQUIRE(mllw.has_value());
  CHECK(mllw->datum() == VerticalDatum::mllw);
  CHECK(mllw->assume_datum(VerticalDatum::mllw) ==
        std::unexpected{MetaError::already_set});
  CHECK(mllw->assume_datum(VerticalDatum::navd88) ==
        std::unexpected{MetaError::already_set});

  const SeriesMeta wind =
      meta_or_default(SeriesMeta::make({.quantity = Quantity::wind_u}));
  CHECK(wind.assume_datum(VerticalDatum::msl) ==
        std::unexpected{MetaError::datum_not_applicable});
}

TEST_CASE("SeriesMeta equality compares every field", "[core][meta]") {
  const SeriesMeta a =
      meta_or_default(SeriesMeta::make({.quantity = Quantity::water_level,
                                        .label = "a",
                                        .unit = LengthUnit::meter}));
  CHECK(a == a.with_label("a"));
  CHECK(a != a.with_label("b"));
  CHECK(a != meta_or_default(
                 SeriesMeta::make({.quantity = Quantity::water_level_prediction,
                                   .label = "a",
                                   .unit = LengthUnit::meter})));
  CHECK(a != meta_or_default(SeriesMeta::make(
                 {.quantity = Quantity::water_level, .label = "a"})));
  CHECK(a != meta_or_default(a.assume_datum(VerticalDatum::msl)));
}
