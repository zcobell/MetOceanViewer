// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"
#include "mov/core/vector_series.hpp"
#include "test_helpers.hpp"

using mov::core::AlignmentErrc;
using mov::core::Dry;
using mov::core::FileStation;
using mov::core::GenericQuantity;
using mov::core::LengthUnit;
using mov::core::Location;
using mov::core::magnitude3;
using mov::core::Missing;
using mov::core::parse_unit;
using mov::core::Quantity;
using mov::core::QuantityId;
using mov::core::Sample;
using mov::core::SeriesMeta;
using mov::core::SpeedUnit;
using mov::core::StationRow;
using mov::core::StationTable;
using mov::core::Time;
using mov::core::TimeSeries;
using mov::core::Unit;
using mov::core::vector_series;
using mov::core::VectorSeries;
using mov::core::VerticalDatum;
using mov::test::at_ms;
using mov::test::near;

namespace {

Sample val(double v) { return Sample::of(v).value_or(Sample{}); }

std::vector<Time> axis(std::initializer_list<std::int64_t> ms) {
  std::vector<Time> out;
  for (const std::int64_t m : ms) {
    out.push_back(at_ms(m));
  }
  return out;
}

GenericQuantity generic(std::string_view tok) {
  return GenericQuantity::parse({.token = tok, .standard_name = ""})
      .value_or(GenericQuantity::value());
}

struct Spec {
  QuantityId quantity;
  std::string label{};
  std::optional<Unit> unit = Unit{SpeedUnit::meter_per_second};
  std::optional<VerticalDatum> datum{};
};

TimeSeries series(const Spec& spec, std::vector<Sample> samples,
                  std::vector<Time> times = {}) {
  if (times.empty()) {
    for (std::size_t i = 0; i < samples.size(); ++i) {
      times.push_back(at_ms(static_cast<std::int64_t>(i) * 1000));
    }
  }
  const auto meta = SeriesMeta::make({.quantity = spec.quantity,
                                      .label = spec.label,
                                      .unit = spec.unit,
                                      .datum = spec.datum});
  REQUIRE(meta.has_value());
  auto s = TimeSeries::make(std::move(times), std::move(samples),
                            meta.value_or(SeriesMeta{}));
  REQUIRE(s.has_value());
  return s.value_or(TimeSeries{});
}

VectorSeries pair(std::vector<Sample> u, std::vector<Sample> v,
                  Quantity qu = Quantity::current_u,
                  Quantity qv = Quantity::current_v) {
  auto made = VectorSeries::make(series({.quantity = qu}, std::move(u)),
                                 series({.quantity = qv}, std::move(v)));
  REQUIRE(made.has_value());
  return std::move(made).value();
}

double only_value(const TimeSeries& s) {
  REQUIRE(s.size() == 1);
  return s.samples()[0].value().value_or(mov::test::quiet_nan);
}

double direction(double u, double v) {
  return only_value(pair({val(u)}, {val(v)}).cartesian_direction());
}

Unit degree() { return parse_unit("degree").value_or(Unit{}); }

// u() and v() view the components, so they need an lvalue.
template <class T>
concept HasComponents = requires(T&& t) {
  std::forward<T>(t).u();
  std::forward<T>(t).v();
};

}  // namespace

TEST_CASE("VectorSeries is a value", "[core][vector_series]") {
  STATIC_REQUIRE(std::copyable<VectorSeries>);
  STATIC_REQUIRE(std::equality_comparable<VectorSeries>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<VectorSeries>);
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<VectorSeries>);
  STATIC_REQUIRE(HasComponents<const VectorSeries&>);
  STATIC_REQUIRE_FALSE(HasComponents<VectorSeries>);
}

TEST_CASE("only registered component pairs form a vector",
          "[core][vector_series]") {
  const auto make = [](QuantityId u, QuantityId v) {
    return VectorSeries::make(series({.quantity = std::move(u)}, {val(1.0)}),
                              series({.quantity = std::move(v)}, {val(2.0)}));
  };
  CHECK(make(Quantity::current_u, Quantity::current_v).has_value());
  CHECK(make(Quantity::wind_u, Quantity::wind_v).has_value());
  CHECK(make(generic("u_velocity"), generic("v_velocity")).has_value());
  CHECK(make(GenericQuantity::value(), GenericQuantity::value()).has_value());

  const auto not_pair = std::unexpected{AlignmentErrc::not_a_vector_pair};
  CHECK(make(Quantity::current_v, Quantity::current_u) == not_pair);
  CHECK(make(Quantity::wind_u, Quantity::current_v) == not_pair);
  CHECK(make(Quantity::current_u, Quantity::current_u) == not_pair);
  CHECK(make(Quantity::wind_u, generic("v")) == not_pair);
  CHECK(make(generic("u"), Quantity::wind_v) == not_pair);
  CHECK(make(Quantity::wind_speed, Quantity::wind_direction) == not_pair);
  CHECK(make(Quantity::water_level, GenericQuantity::value()) == not_pair);
}

TEST_CASE("components must share times, unit and datum",
          "[core][vector_series]") {
  const Spec u{.quantity = Quantity::wind_u};
  const Spec v{.quantity = Quantity::wind_v};
  CHECK(VectorSeries::make(series(u, {val(1.0)}, axis({0})),
                           series(v, {val(1.0)}, axis({1}))) ==
        std::unexpected{AlignmentErrc::times_differ});
  CHECK(VectorSeries::make(series(u, {val(1.0)}),
                           series(v, {val(1.0), val(2.0)})) ==
        std::unexpected{AlignmentErrc::times_differ});
  CHECK(VectorSeries::make(
            series({.quantity = Quantity::wind_u, .unit = std::nullopt},
                   {val(1.0)}),
            series(v, {val(1.0)})) ==
        std::unexpected{AlignmentErrc::unit_unknown});
  CHECK(VectorSeries::make(
            series(u, {val(1.0)}),
            series({.quantity = Quantity::wind_v, .unit = std::nullopt},
                   {val(1.0)})) ==
        std::unexpected{AlignmentErrc::unit_unknown});
  CHECK(
      VectorSeries::make(
          series(u, {val(1.0)}),
          series({.quantity = Quantity::wind_v, .unit = Unit{SpeedUnit::knot}},
                 {val(1.0)})) == std::unexpected{AlignmentErrc::units_differ});

  // Only generic components can carry a datum.
  const auto with_datum = [](std::optional<VerticalDatum> d) {
    return series({.quantity = GenericQuantity::value(), .datum = d},
                  {val(1.0)});
  };
  CHECK(VectorSeries::make(with_datum(VerticalDatum::msl),
                           with_datum(VerticalDatum::msl))
            .has_value());
  CHECK(VectorSeries::make(with_datum(VerticalDatum::msl),
                           with_datum(VerticalDatum::navd88)) ==
        std::unexpected{AlignmentErrc::datums_differ});
  CHECK(VectorSeries::make(with_datum(std::nullopt),
                           with_datum(VerticalDatum::navd88)) ==
        std::unexpected{AlignmentErrc::datum_unknown});
  CHECK(VectorSeries::make(with_datum(VerticalDatum::msl),
                           with_datum(std::nullopt)) ==
        std::unexpected{AlignmentErrc::datum_unknown});

  // Pinned order: pair, times, unit.
  CHECK(VectorSeries::make(
            series({.quantity = Quantity::wind_u, .unit = std::nullopt},
                   {val(1.0)}, axis({0})),
            series({.quantity = Quantity::current_v}, {val(1.0)}, axis({1}))) ==
        std::unexpected{AlignmentErrc::not_a_vector_pair});
  CHECK(VectorSeries::make(
            series({.quantity = Quantity::wind_u, .unit = std::nullopt},
                   {val(1.0)}, axis({0})),
            series(v, {val(1.0)}, axis({1}))) ==
        std::unexpected{AlignmentErrc::times_differ});
}

TEST_CASE("the components are kept as given", "[core][vector_series]") {
  const TimeSeries u = series({.quantity = Quantity::current_u}, {val(1.0)});
  const TimeSeries v = series({.quantity = Quantity::current_v}, {val(2.0)});
  const auto vs = VectorSeries::make(u, v);
  REQUIRE(vs.has_value());
  CHECK(vs->u() == u);
  CHECK(vs->v() == v);
}

TEST_CASE("magnitude is the Euclidean norm [regression][B1]",
          "[core][vector_series][regression][B1]") {
  // fort.62 station 1, step 1: v4 computed (u^2 + v^2)^2 = 0.0025...
  const double m = only_value(
      pair({val(0.049298094833)}, {val(0.012227184139)}).magnitude());
  CHECK(near(m, std::sqrt(0.049298094833 * 0.049298094833 +
                          0.012227184139 * 0.012227184139)));
  CHECK(near(m, 0.0507918, 1e-6));
  CHECK(near(only_value(pair({val(3.0)}, {val(-4.0)}).magnitude()), 5.0));
}

TEST_CASE("a missing or dry component decides the result [regression][N7]",
          "[core][vector_series][regression][N7]") {
  const VectorSeries vs =
      pair({val(1.0), Sample{}, Sample{Dry{}}, Sample{}, val(1.0)},
           {Sample{}, val(1.0), val(1.0), Sample{Dry{}}, Sample{Dry{}}});
  const std::vector<Sample> expected{Sample{}, Sample{}, Sample{Dry{}},
                                     Sample{}, Sample{Dry{}}};
  const TimeSeries m = vs.magnitude();
  const TimeSeries d = vs.cartesian_direction();
  CHECK(std::vector<Sample>(m.samples().begin(), m.samples().end()) ==
        expected);
  CHECK(std::vector<Sample>(d.samples().begin(), d.samples().end()) ==
        expected);
  CHECK(std::vector<Time>(m.times().begin(), m.times().end()) ==
        std::vector<Time>(vs.u().times().begin(), vs.u().times().end()));
}

TEST_CASE("cartesian direction is atan2(v, u) in (-180, 180]",
          "[core][vector_series]") {
  CHECK(direction(1.0, 0.0) == 0.0);
  CHECK(near(direction(0.0, 1.0), 90.0));
  CHECK(near(direction(1.0, 1.0), 45.0));
  CHECK(near(direction(0.0, -1.0), -90.0));
  CHECK(near(direction(-1.0, -1.0), -135.0));
  CHECK(near(direction(-1.0, 0.0), 180.0));
  // atan2(-0.0, -1) is -pi: the half-open range maps it to +180.
  CHECK(direction(-1.0, -0.0) == direction(-1.0, 0.0));
  CHECK(direction(-1.0, -1e-300) == direction(-1.0, 0.0));
}

TEST_CASE("a zero vector has no direction", "[core][vector_series]") {
  const TimeSeries d =
      pair({val(0.0), val(-0.0), val(0.0)}, {val(0.0), val(0.0), val(1e-300)})
          .cartesian_direction();
  CHECK(d.samples()[0] == Sample{Missing{}});
  CHECK(d.samples()[1] == Sample{Missing{}});
  CHECK(near(d.samples()[2].value().value_or(0.0), 90.0));
}

TEST_CASE("derived series carry the metadata table of section 2.9",
          "[core][vector_series]") {
  const auto meta_of = [](const TimeSeries& s) { return s.meta(); };
  const auto expect = [](QuantityId q, std::string label, Unit unit) {
    return SeriesMeta::make({.quantity = std::move(q),
                             .label = std::move(label),
                             .unit = std::move(unit)})
        .value_or(SeriesMeta{});
  };
  const std::string cartesian =
      " direction (cartesian: degrees counter-clockwise from east, toward)";

  const VectorSeries wind =
      pair({val(1.0)}, {val(1.0)}, Quantity::wind_u, Quantity::wind_v);
  CHECK(meta_of(wind.magnitude()) == expect(Quantity::wind_speed, "wind speed",
                                            SpeedUnit::meter_per_second));
  // Never wind_direction: that is the meteorological "from" direction.
  CHECK(meta_of(wind.cartesian_direction()) ==
        expect(GenericQuantity::value(), "wind" + cartesian, degree()));

  const VectorSeries current = pair({val(1.0)}, {val(1.0)});
  CHECK(meta_of(current.magnitude()) == expect(GenericQuantity::value(),
                                               "current speed",
                                               SpeedUnit::meter_per_second));
  CHECK(meta_of(current.cartesian_direction()) ==
        expect(GenericQuantity::value(), "current" + cartesian, degree()));

  // Generic pairs: the stem is the u label without a trailing u/x component.
  const auto generic_pair = [](std::string u_label) {
    auto vs = VectorSeries::make(series({.quantity = GenericQuantity::value(),
                                         .label = std::move(u_label),
                                         .unit = Unit{LengthUnit::meter},
                                         .datum = VerticalDatum::msl},
                                        {val(1.0)}),
                                 series({.quantity = GenericQuantity::value(),
                                         .label = "ignored",
                                         .unit = Unit{LengthUnit::meter},
                                         .datum = VerticalDatum::msl},
                                        {val(1.0)}));
    REQUIRE(vs.has_value());
    return std::move(vs).value();
  };
  // The datum is never carried over.
  CHECK(meta_of(generic_pair("velocity u").magnitude()) ==
        expect(GenericQuantity::value(), "velocity speed", LengthUnit::meter));
  CHECK(meta_of(generic_pair("flow_X").cartesian_direction()) ==
        expect(GenericQuantity::value(), "flow" + cartesian, degree()));
  const auto speed_label = [&generic_pair](std::string u_label) {
    const TimeSeries speed = generic_pair(std::move(u_label)).magnitude();
    return std::string{speed.meta().label()};
  };
  CHECK(speed_label("drift-u") == "drift speed");
  CHECK(speed_label("Drift") == "Drift speed");
  CHECK(speed_label("menu") == "menu speed");
  CHECK(speed_label("u") == "vector speed");
  CHECK(speed_label("") == "vector speed");
  CHECK(speed_label("  ") == "vector speed");
}

TEST_CASE("vector_series pairs two columns of a table",
          "[core][vector_series]") {
  const auto speed = [](Quantity q) {
    return SeriesMeta::make(
               {.quantity = q, .unit = SpeedUnit::meter_per_second})
        .value_or(SeriesMeta{});
  };
  const auto table = StationTable::make(
      {speed(Quantity::current_u), speed(Quantity::current_v),
       SeriesMeta::make({.quantity = Quantity::water_level})
           .value_or(SeriesMeta{})},
      {axis({0, 1})},
      {StationRow{
          .station =
              FileStation{
                  .id = "0",
                  .name = "Station 0",
                  .location = Location::make({.lat = 0.0, .lon = 0.0}).value(),
                  .native = std::nullopt,
                  .source = std::nullopt},
          .axis = 0,
          .columns = {{val(3.0), val(0.0)},
                      {val(4.0), val(2.0)},
                      {val(1.0), val(1.0)}}}});
  REQUIRE(table.has_value());
  const auto vs = vector_series(*table, 0, 0, 1);
  REQUIRE(vs.has_value());
  CHECK(vs->u() == table->series(0, 0));
  const TimeSeries magnitude = vs->magnitude();
  CHECK(near(magnitude.samples()[0].value().value_or(0.0), 5.0));
  CHECK(vector_series(*table, 0, 1, 0) ==
        std::unexpected{AlignmentErrc::not_a_vector_pair});
  CHECK(vector_series(*table, 0, 0, 2) ==
        std::unexpected{AlignmentErrc::not_a_vector_pair});
}

TEST_CASE("magnitude3 is the 3D current speed", "[core][vector_series]") {
  const TimeSeries x =
      series({.quantity = Quantity::current_u}, {val(1.0), Sample{}});
  const TimeSeries y =
      series({.quantity = Quantity::current_v}, {val(2.0), val(1.0)});
  const TimeSeries z =
      series({.quantity = generic("upward_velocity")}, {val(2.0), val(1.0)});
  const auto m = magnitude3(x, y, z);
  REQUIRE(m.has_value());
  CHECK(near(m->samples()[0].value().value_or(0.0), 3.0));
  CHECK(m->samples()[1] == Sample{Missing{}});
  CHECK(m->meta() == SeriesMeta::make({.quantity = GenericQuantity::value(),
                                       .label = "3D current speed",
                                       .unit = SpeedUnit::meter_per_second})
                         .value_or(SeriesMeta{}));
  CHECK(m->times().size() == 2);
}

TEST_CASE("magnitude3 checks its components", "[core][vector_series]") {
  const TimeSeries x = series({.quantity = Quantity::current_u}, {val(1.0)});
  const TimeSeries y = series({.quantity = Quantity::current_v}, {val(1.0)});
  const TimeSeries z = series({.quantity = generic("w")}, {val(1.0)});
  CHECK(magnitude3(y, x, z) ==
        std::unexpected{AlignmentErrc::not_a_vector_pair});
  CHECK(magnitude3(x, y,
                   series({.quantity = Quantity::water_level}, {val(1.0)})) ==
        std::unexpected{AlignmentErrc::not_a_vector_pair});
  CHECK(magnitude3(series({.quantity = Quantity::wind_u}, {val(1.0)}),
                   series({.quantity = Quantity::wind_v}, {val(1.0)}),
                   z) == std::unexpected{AlignmentErrc::not_a_vector_pair});
  CHECK(magnitude3(x, y,
                   series({.quantity = generic("w")}, {val(1.0)}, axis({5}))) ==
        std::unexpected{AlignmentErrc::times_differ});
  CHECK(magnitude3(
            x, y,
            series({.quantity = generic("w"), .unit = Unit{SpeedUnit::knot}},
                   {val(1.0)})) ==
        std::unexpected{AlignmentErrc::units_differ});
  CHECK(magnitude3(x, y,
                   series({.quantity = generic("w"), .unit = std::nullopt},
                          {val(1.0)})) ==
        std::unexpected{AlignmentErrc::unit_unknown});
  CHECK(
      magnitude3(x, y,
                 series({.quantity = generic("w"), .datum = VerticalDatum::msl},
                        {val(1.0)})) ==
      std::unexpected{AlignmentErrc::datum_unknown});
}
