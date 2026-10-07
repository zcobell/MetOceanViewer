// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/vector_series.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/detail/ascii.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/units.hpp"

namespace mov::core {

namespace {

enum class PairKind : std::uint8_t { wind, current, generic };

bool is(const QuantityId& q, Quantity expected) noexcept {
  const Quantity* registry = std::get_if<Quantity>(&q);
  return registry != nullptr and *registry == expected;
}

bool is_generic(const QuantityId& q) noexcept {
  return std::holds_alternative<GenericQuantity>(q);
}

std::optional<PairKind> pair_kind(const QuantityId& u,
                                  const QuantityId& v) noexcept {
  if (is_generic(u) and is_generic(v)) {
    return PairKind::generic;
  }
  if (is(u, Quantity::wind_u) and is(v, Quantity::wind_v)) {
    return PairKind::wind;
  }
  if (is(u, Quantity::current_u) and is(v, Quantity::current_v)) {
    return PairKind::current;
  }
  return std::nullopt;
}

std::optional<AlignmentErrc> check_units(const SeriesMeta& a,
                                         const SeriesMeta& b) {
  if (not a.unit() or not b.unit()) {
    return AlignmentErrc::unit_unknown;
  }
  if (*a.unit() != *b.unit()) {
    return AlignmentErrc::units_differ;
  }
  return std::nullopt;
}

std::optional<AlignmentErrc> check_datums(const SeriesMeta& a,
                                          const SeriesMeta& b) noexcept {
  if (a.datum() == b.datum()) {
    return std::nullopt;
  }
  return (a.datum() and b.datum()) ? AlignmentErrc::datums_differ
                                   : AlignmentErrc::datum_unknown;
}

// Times, then units, then datums.
std::optional<AlignmentErrc> check_aligned(const TimeSeries& a,
                                           const TimeSeries& b) {
  if (not std::ranges::equal(a.times(), b.times())) {
    return AlignmentErrc::times_differ;
  }
  if (const auto e = check_units(a.meta(), b.meta())) {
    return e;
  }
  return check_datums(a.meta(), b.meta());
}

bool is_component_letter(char c) noexcept {
  return c == 'u' or c == 'U' or c == 'x' or c == 'X';
}

bool is_separator(char c) noexcept { return c == ' ' or c == '_' or c == '-'; }

// The label of a generic u component without its component suffix:
// "velocity u" -> "velocity", "flow_X" -> "flow"; "" or "u" -> "vector".
std::string generic_stem(std::string_view label) {
  label = detail::trim(label);
  if (label.size() == 1 and is_component_letter(label.front())) {
    label = {};
  } else if (label.size() >= 2 and is_component_letter(label.back()) and
             is_separator(label[label.size() - 2])) {
    label = detail::trim(label.substr(0, label.size() - 2));
  }
  return label.empty() ? std::string{"vector"} : std::string{label};
}

std::string stem(PairKind kind, std::string_view u_label) {
  if (kind == PairKind::wind) {
    return "wind";
  }
  if (kind == PairKind::current) {
    return "current";
  }
  return generic_stem(u_label);
}

// A derived series has no datum, so make cannot fail.
SeriesMeta derived_meta(QuantityId quantity, std::string label, Unit unit) {
  return SeriesMeta::make({.quantity = std::move(quantity),
                           .label = std::move(label),
                           .unit = std::move(unit),
                           .datum = std::nullopt})
      .value_or(SeriesMeta{});
}

Unit degree() { return parse_unit("degree").value_or(Unit{}); }

template <class Op>
std::vector<Sample> zip_samples(const TimeSeries& a, const TimeSeries& b,
                                Op op) {
  std::vector<Sample> out(a.size());
  std::ranges::transform(a.samples(), b.samples(), out.begin(), op);
  return out;
}

Sample hypot2(Sample a, Sample b) noexcept {
  return combine(a, b, [](double x, double y) { return std::hypot(x, y); });
}

// atan2(v, u) in degrees, in (-180, 180]; a zero vector is Missing.
Sample direction(Sample u, Sample v) noexcept {
  const std::optional<double> x = u.value();
  const std::optional<double> y = v.value();
  if (x and y and *x == 0.0 and *y == 0.0) {
    return Sample{Missing{}};
  }
  return combine(u, v, [](double east, double north) {
    double radians = std::atan2(north, east);
    if (radians == -std::numbers::pi) {
      radians = std::numbers::pi;
    }
    return radians * (180.0 / std::numbers::pi);
  });
}

std::vector<Time> times_of(const TimeSeries& s) {
  return {s.times().begin(), s.times().end()};
}

}  // namespace

std::expected<VectorSeries, AlignmentErrc> VectorSeries::make(TimeSeries u,
                                                              TimeSeries v) {
  if (not pair_kind(u.meta().quantity(), v.meta().quantity())) {
    return std::unexpected{AlignmentErrc::not_a_vector_pair};
  }
  if (const auto e = check_aligned(u, v)) {
    return std::unexpected{*e};
  }
  return VectorSeries{std::move(u), std::move(v)};
}

TimeSeries VectorSeries::magnitude() const {
  const PairKind kind = pair_kind(u_.meta().quantity(), v_.meta().quantity())
                            .value_or(PairKind::generic);
  const Unit unit = u_.meta().unit().value_or(Unit{});
  SeriesMeta meta =
      kind == PairKind::wind
          ? derived_meta(Quantity::wind_speed, "wind speed", unit)
          : derived_meta(GenericQuantity::value(),
                         stem(kind, u_.meta().label()) + " speed", unit);
  return detail::trusted_series(times_of(u_), zip_samples(u_, v_, hypot2),
                                std::move(meta));
}

TimeSeries VectorSeries::cartesian_direction() const {
  const PairKind kind = pair_kind(u_.meta().quantity(), v_.meta().quantity())
                            .value_or(PairKind::generic);
  SeriesMeta meta = derived_meta(
      GenericQuantity::value(),
      stem(kind, u_.meta().label()) +
          " direction (cartesian: degrees counter-clockwise from east, toward)",
      degree());
  return detail::trusted_series(times_of(u_), zip_samples(u_, v_, direction),
                                std::move(meta));
}

std::expected<VectorSeries, AlignmentErrc> vector_series(const StationTable& t,
                                                         std::size_t station,
                                                         std::size_t ku,
                                                         std::size_t kv) {
  return VectorSeries::make(t.series(station, ku), t.series(station, kv));
}

std::expected<TimeSeries, AlignmentErrc> magnitude3(const TimeSeries& x,
                                                    const TimeSeries& y,
                                                    const TimeSeries& z) {
  const auto kind = pair_kind(x.meta().quantity(), y.meta().quantity());
  if (not kind or kind == PairKind::wind or
      not is_generic(z.meta().quantity())) {
    return std::unexpected{AlignmentErrc::not_a_vector_pair};
  }
  for (const TimeSeries* other : {&y, &z}) {
    if (const auto e = check_aligned(x, *other)) {
      return std::unexpected{*e};
    }
  }
  std::vector<Sample> horizontal = zip_samples(x, y, hypot2);
  std::vector<Sample> speed(x.size());
  std::ranges::transform(horizontal, z.samples(), speed.begin(), hypot2);
  return detail::trusted_series(
      times_of(x), std::move(speed),
      derived_meta(GenericQuantity::value(), "3D current speed",
                   x.meta().unit().value_or(Unit{})));
}

}  // namespace mov::core
