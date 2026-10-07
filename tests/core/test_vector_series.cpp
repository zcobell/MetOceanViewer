// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <concepts>
#include <cstddef>
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

using mov::core::ColumnIndex;
using mov::core::degree;
using mov::core::Dry;
using mov::core::FileStation;
using mov::core::GenericQuantity;
using mov::core::LengthUnit;
using mov::core::Location;
using mov::core::magnitude3;
using mov::core::Missing;
using mov::core::Quantity;
using mov::core::QuantityId;
using mov::core::Sample;
using mov::core::SeriesMeta;
using mov::core::SpeedUnit;
using mov::core::StationIndex;
using mov::core::StationRow;
using mov::core::StationTable;
using mov::core::Time;
using mov::core::TimeAxis;
using mov::core::TimeSeries;
using mov::core::Unit;
using mov::core::Variable;
using mov::core::vector_series;
using mov::core::VectorErrc;
using mov::core::VectorKind;
using mov::core::VectorSeries;
using mov::core::VerticalDatum;
using mov::core::VerticalErrc;
using mov::test::at_ms;
using mov::test::near;

namespace {

Sample val(double v) { return Sample::of(v).value_or(Sample{}); }

TimeAxis axis(std::initializer_list<std::int64_t> ms) {
  TimeAxis out;
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
};

TimeSeries series(const Spec& spec, std::vector<Sample> samples,
                  TimeAxis times = {}) {
  if (times.empty()) {
    for (std::size_t i = 0; i < samples.size(); ++i) {
      times.push_back(at_ms(static_cast<std::int64_t>(i) * 1000));
    }
  }
  auto s = TimeSeries::make(
      std::move(times), std::move(samples),
      SeriesMeta::make(
          {.quantity = spec.quantity, .label = spec.label, .unit = spec.unit}));
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

VectorSeries generic_pair(std::string u_label, Unit unit = LengthUnit::meter) {
  auto vs = VectorSeries::assume_components(
      series({.quantity = GenericQuantity::value(),
              .label = std::move(u_label),
              .unit = unit},
             {val(1.0)}),
      series({.quantity = GenericQuantity::value(),
              .label = "ignored",
              .unit = unit},
             {val(1.0)}));
  REQUIRE(vs.has_value());
  return std::move(vs).value();
}

double only_value(const TimeSeries& s) {
  REQUIRE(s.size() == 1);
  return s.samples()[0].value().value_or(mov::test::quiet_nan);
}

double direction(double u, double v) {
  return only_value(pair({val(u)}, {val(v)}).cartesian_direction());
}

std::string speed_label(const VectorSeries& vs) {
  const TimeSeries speed = vs.magnitude();
  return std::string{speed.meta().label()};
}

// u() and v() view the components, so they need an lvalue.
template <class T>
concept HasComponents = requires(T&& t) {
  std::forward<T>(t).u();
  std::forward<T>(t).v();
  std::forward<T>(t).unit();
};

}  // namespace

TEST_CASE("VectorSeries is a value", "[core][vector_series]") {
  STATIC_REQUIRE(std::copyable<VectorSeries>);
  STATIC_REQUIRE(std::equality_comparable<VectorSeries>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<VectorSeries>);
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<VectorSeries>);
  STATIC_REQUIRE(HasComponents<const VectorSeries&>);
  STATIC_REQUIRE_FALSE(HasComponents<VectorSeries>);
  STATIC_REQUIRE_FALSE(HasComponents<const VectorSeries>);
}

TEST_CASE("make accepts the registered component pairs only",
          "[core][vector_series]") {
  const auto make = [](QuantityId u, QuantityId v) {
    return VectorSeries::make(series({.quantity = std::move(u)}, {val(1.0)}),
                              series({.quantity = std::move(v)}, {val(2.0)}));
  };
  const auto current = make(Quantity::current_u, Quantity::current_v);
  REQUIRE(current.has_value());
  CHECK(current->kind() == VectorKind::current);
  CHECK(current->unit() == Unit{SpeedUnit::meter_per_second});
  const auto wind = make(Quantity::wind_u, Quantity::wind_v);
  REQUIRE(wind.has_value());
  CHECK(wind->kind() == VectorKind::wind);

  const auto not_pair = std::unexpected{VectorErrc::not_a_vector_pair};
  // Generic components need the named factory.
  CHECK(make(generic("u_velocity"), generic("v_velocity")) == not_pair);
  CHECK(make(GenericQuantity::value(), GenericQuantity::value()) == not_pair);
  CHECK(make(Quantity::current_v, Quantity::current_u) == not_pair);
  CHECK(make(Quantity::wind_u, Quantity::current_v) == not_pair);
  CHECK(make(Quantity::current_u, Quantity::current_u) == not_pair);
  CHECK(make(Quantity::wind_u, generic("v")) == not_pair);
  CHECK(make(Quantity::wind_speed, Quantity::wind_direction) == not_pair);
}

TEST_CASE("assume_components takes two generic series",
          "[core][vector_series]") {
  const auto assume = [](QuantityId u, QuantityId v) {
    return VectorSeries::assume_components(
        series({.quantity = std::move(u)}, {val(1.0)}),
        series({.quantity = std::move(v)}, {val(2.0)}));
  };
  const auto vs = assume(generic("u_velocity"), generic("v_velocity"));
  REQUIRE(vs.has_value());
  CHECK(vs->kind() == VectorKind::generic);
  CHECK(assume(GenericQuantity::value(), GenericQuantity::value()).has_value());

  const auto not_pair = std::unexpected{VectorErrc::not_a_vector_pair};
  CHECK(assume(Quantity::current_u, Quantity::current_v) == not_pair);
  CHECK(assume(generic("u"), Quantity::wind_v) == not_pair);
  CHECK(assume(Quantity::water_level, GenericQuantity::value()) == not_pair);
}

TEST_CASE("components must share times and unit", "[core][vector_series]") {
  const Spec u{.quantity = Quantity::wind_u};
  const Spec v{.quantity = Quantity::wind_v};
  CHECK(VectorSeries::make(series(u, {val(1.0)}, axis({0})),
                           series(v, {val(1.0)}, axis({1}))) ==
        std::unexpected{VectorErrc::times_differ});
  CHECK(VectorSeries::make(series(u, {val(1.0)}),
                           series(v, {val(1.0), val(2.0)})) ==
        std::unexpected{VectorErrc::times_differ});
  const Spec u_unknown{.quantity = Quantity::wind_u, .unit = std::nullopt};
  const Spec v_unknown{.quantity = Quantity::wind_v, .unit = std::nullopt};
  CHECK(VectorSeries::make(series(u_unknown, {val(1.0)}),
                           series(v, {val(1.0)})) ==
        std::unexpected{VectorErrc::unit_unknown});
  CHECK(VectorSeries::make(series(u, {val(1.0)}),
                           series(v_unknown, {val(1.0)})) ==
        std::unexpected{VectorErrc::unit_unknown});
  CHECK(
      VectorSeries::make(
          series(u, {val(1.0)}),
          series({.quantity = Quantity::wind_v, .unit = Unit{SpeedUnit::knot}},
                 {val(1.0)})) == std::unexpected{VectorErrc::units_differ});

  // Pinned order: pair, times, unit.
  CHECK(VectorSeries::make(
            series(u_unknown, {val(1.0)}, axis({0})),
            series({.quantity = Quantity::current_v}, {val(1.0)}, axis({1}))) ==
        std::unexpected{VectorErrc::not_a_vector_pair});
  CHECK(VectorSeries::make(series(u_unknown, {val(1.0)}, axis({0})),
                           series(v, {val(1.0)}, axis({1}))) ==
        std::unexpected{VectorErrc::times_differ});
}

TEST_CASE("component datums are not compared", "[core][vector_series]") {
  const auto with_datum = [](VerticalDatum d) {
    return series({.quantity = GenericQuantity::value(),
                   .unit = Unit{LengthUnit::meter}},
                  {val(1.0)})
        .assume_datum(d)
        .value_or(TimeSeries{});
  };
  const auto vs = VectorSeries::assume_components(
      with_datum(VerticalDatum::msl), with_datum(VerticalDatum::navd88));
  REQUIRE(vs.has_value());
  const TimeSeries m = vs->magnitude();
  CHECK(not m.meta().datum().has_value());
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
  CHECK(TimeAxis(m.times().begin(), m.times().end()) ==
        TimeAxis(vs.u().times().begin(), vs.u().times().end()));
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
                             .unit = std::move(unit)});
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
  CHECK(meta_of(generic_pair("velocity u").magnitude()) ==
        expect(GenericQuantity::value(), "velocity speed", LengthUnit::meter));
  CHECK(meta_of(generic_pair("flow_X").cartesian_direction()) ==
        expect(GenericQuantity::value(), "flow" + cartesian, degree()));
  CHECK(speed_label(generic_pair("drift-u")) == "drift speed");
  CHECK(speed_label(generic_pair("Drift")) == "Drift speed");
  CHECK(speed_label(generic_pair("menu")) == "menu speed");
  CHECK(speed_label(generic_pair("u")) == "vector speed");
  CHECK(speed_label(generic_pair("")) == "vector speed");
  CHECK(speed_label(generic_pair("  ")) == "vector speed");
}

TEST_CASE("vector_series pairs two columns of a table",
          "[core][vector_series]") {
  const auto speed = [](Quantity q) {
    return SeriesMeta::make(
        {.quantity = q, .unit = SpeedUnit::meter_per_second});
  };
  const auto table = StationTable::make(
      {Variable{.meta = speed(Quantity::current_u),
                .per_station = {{val(3.0), val(0.0)}}},
       Variable{.meta = speed(Quantity::current_v),
                .per_station = {{val(4.0), val(2.0)}}},
       Variable{.meta = SeriesMeta::make({.quantity = Quantity::water_level}),
                .per_station = {{val(1.0), val(1.0)}}}},
      {axis({0, 1})},
      {StationRow{
          .station =
              FileStation{
                  .id = mov::core::StationKey::make("0").value(),
                  .name = mov::core::StationText{},
                  .location = Location::make({.lat = 0.0, .lon = 0.0}).value(),
                  .native = std::nullopt,
                  .source = std::nullopt},
          .axis = 0}});
  REQUIRE(table.has_value());
  const StationIndex s0{0};
  const auto vs = vector_series(*table, s0, ColumnIndex{0}, ColumnIndex{1});
  REQUIRE(vs.has_value());
  CHECK(vs->u() == table->series(s0, ColumnIndex{0}));
  const TimeSeries magnitude = vs->magnitude();
  CHECK(near(magnitude.samples()[0].value().value_or(0.0), 5.0));
  CHECK(vector_series(*table, s0, ColumnIndex{1}, ColumnIndex{0}) ==
        std::unexpected{VectorErrc::not_a_vector_pair});
  CHECK(vector_series(*table, s0, ColumnIndex{0}, ColumnIndex{2}) ==
        std::unexpected{VectorErrc::not_a_vector_pair});
}

TEST_CASE("magnitude3 is std::hypot of three components",
          "[core][vector_series]") {
  const VectorSeries horizontal =
      pair({val(2.0), Sample{}, val(1.0)}, {val(3.0), val(1.0), Sample{Dry{}}});
  const TimeSeries w = series({.quantity = generic("upward_velocity")},
                              {val(6.0), val(1.0), val(1.0)});
  const auto m = magnitude3(horizontal, w);
  REQUIRE(m.has_value());
  CHECK(m->samples()[0] == Sample::of(std::hypot(2.0, 3.0, 6.0)));
  CHECK(m->samples()[0] == Sample::of(7.0));
  CHECK(m->samples()[1] == Sample{Missing{}});
  CHECK(m->samples()[2] == Sample{Dry{}});
  CHECK(m->meta() == SeriesMeta::make({.quantity = GenericQuantity::value(),
                                       .label = "3D current speed",
                                       .unit = SpeedUnit::meter_per_second}));
  CHECK(m->times().size() == 3);

  // A wet w does not hide a Missing or Dry horizontal sample, and vice versa.
  const auto dry_w =
      magnitude3(pair({val(1.0)}, {val(1.0)}),
                 series({.quantity = generic("w")}, {Sample{Dry{}}}));
  REQUIRE(dry_w.has_value());
  CHECK(dry_w->samples()[0] == Sample{Dry{}});
}

TEST_CASE("magnitude3 labels follow the pair kind", "[core][vector_series]") {
  const TimeSeries w = series({.quantity = generic("w")}, {val(1.0)});
  const auto wind = magnitude3(
      pair({val(1.0)}, {val(1.0)}, Quantity::wind_u, Quantity::wind_v), w);
  REQUIRE(wind.has_value());
  CHECK(wind->meta().label() == "3D wind speed");
  const auto flow =
      magnitude3(generic_pair("flow u", Unit{SpeedUnit::meter_per_second}), w);
  REQUIRE(flow.has_value());
  CHECK(flow->meta().label() == "3D flow speed");
}

TEST_CASE("magnitude3 checks the vertical component", "[core][vector_series]") {
  const VectorSeries horizontal = pair({val(1.0)}, {val(1.0)});
  CHECK(magnitude3(horizontal,
                   series({.quantity = Quantity::water_level}, {val(1.0)})) ==
        std::unexpected{VerticalErrc::not_generic});
  CHECK(magnitude3(horizontal,
                   series({.quantity = generic("w")}, {val(1.0)}, axis({5}))) ==
        std::unexpected{VerticalErrc::times_differ});
  CHECK(magnitude3(horizontal,
                   series({.quantity = generic("w"), .unit = std::nullopt},
                          {val(1.0)})) ==
        std::unexpected{VerticalErrc::unit_unknown});
  CHECK(magnitude3(
            horizontal,
            series({.quantity = generic("w"), .unit = Unit{SpeedUnit::knot}},
                   {val(1.0)})) == std::unexpected{VerticalErrc::units_differ});
}
