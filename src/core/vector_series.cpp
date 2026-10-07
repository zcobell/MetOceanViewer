// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/vector_series.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "core_access.hpp"
#include "mov/core/detail/ascii.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/units.hpp"

namespace mov::core {

namespace {

bool is(const QuantityId& q, Quantity expected) noexcept {
  const Quantity* registry = std::get_if<Quantity>(&q);
  return registry != nullptr and *registry == expected;
}

bool is_generic(const QuantityId& q) noexcept {
  return std::holds_alternative<GenericQuantity>(q);
}

std::optional<VectorKind> registered_pair(const QuantityId& u,
                                          const QuantityId& v) noexcept {
  if (is(u, Quantity::wind_u) and is(v, Quantity::wind_v)) {
    return VectorKind::wind;
  }
  if (is(u, Quantity::current_u) and is(v, Quantity::current_v)) {
    return VectorKind::current;
  }
  return std::nullopt;
}

// The common unit of two series on the same times; times first, then units.
template <class Errc>
std::expected<Unit, Errc> aligned_unit(const TimeSeries& a,
                                       const TimeSeries& b) {
  if (not std::ranges::equal(a.times(), b.times())) {
    return std::unexpected{Errc::times_differ};
  }
  const std::optional<Unit>& ua = a.meta().unit();
  const std::optional<Unit>& ub = b.meta().unit();
  if (not ua or not ub) {
    return std::unexpected{Errc::unit_unknown};
  }
  if (*ua != *ub) {
    return std::unexpected{Errc::units_differ};
  }
  return *ua;
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

std::string stem(VectorKind kind, std::string_view u_label) {
  if (kind == VectorKind::wind) {
    return "wind";
  }
  if (kind == VectorKind::current) {
    return "current";
  }
  return generic_stem(u_label);
}

TimeSeries derived(const TimeSeries& axis_source, std::vector<Sample> samples,
                   SeriesMeta meta) {
  const std::span<const Time> t = axis_source.times();
  return TimeSeries{detail::CoreAccess::key(), TimeAxis(t.begin(), t.end()),
                    std::move(samples), std::move(meta)};
}

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

// The combine rule over three samples, with std::hypot of three.
Sample hypot3(Sample x, Sample y, Sample z) noexcept {
  const std::optional<double> a = x.value();
  const std::optional<double> b = y.value();
  const std::optional<double> c = z.value();
  if (a and b and c) {
    return finite_or_missing(std::hypot(*a, *b, *c));
  }
  const bool missing = x.is_missing() or y.is_missing() or z.is_missing();
  return missing ? Sample{Missing{}} : Sample{Dry{}};
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

}  // namespace

std::expected<VectorSeries, VectorErrc> VectorSeries::make(TimeSeries u,
                                                           TimeSeries v) {
  const auto kind = registered_pair(u.meta().quantity(), v.meta().quantity());
  if (not kind) {
    return std::unexpected{VectorErrc::not_a_vector_pair};
  }
  return aligned_unit<VectorErrc>(u, v).transform([&](Unit unit) {
    return VectorSeries{std::move(u), std::move(v), *kind, std::move(unit)};
  });
}

std::expected<VectorSeries, VectorErrc> VectorSeries::assume_components(
    TimeSeries u, TimeSeries v) {
  if (not is_generic(u.meta().quantity()) or
      not is_generic(v.meta().quantity())) {
    return std::unexpected{VectorErrc::not_a_vector_pair};
  }
  return aligned_unit<VectorErrc>(u, v).transform([&](Unit unit) {
    return VectorSeries{std::move(u), std::move(v), VectorKind::generic,
                        std::move(unit)};
  });
}

TimeSeries VectorSeries::magnitude() const {
  SeriesMeta meta =
      kind_ == VectorKind::wind
          ? SeriesMeta::make({.quantity = Quantity::wind_speed,
                              .label = "wind speed",
                              .unit = unit_})
          : SeriesMeta::make(
                {.quantity = GenericQuantity::value(),
                 .label = stem(kind_, u_.meta().label()) + " speed",
                 .unit = unit_});
  return derived(u_, zip_samples(u_, v_, hypot2), std::move(meta));
}

TimeSeries VectorSeries::cartesian_direction() const {
  SeriesMeta meta = SeriesMeta::make(
      {.quantity = GenericQuantity::value(),
       .label = stem(kind_, u_.meta().label()) +
                " direction (cartesian: degrees counter-clockwise from east, "
                "toward)",
       .unit = degree()});
  return derived(u_, zip_samples(u_, v_, direction), std::move(meta));
}

std::expected<VectorSeries, VectorErrc> vector_series(const StationTable& t,
                                                      StationIndex station,
                                                      ColumnIndex ku,
                                                      ColumnIndex kv) {
  return VectorSeries::make(t.series(station, ku), t.series(station, kv));
}

std::expected<TimeSeries, VerticalErrc> magnitude3(
    const VectorSeries& horizontal, const TimeSeries& w) {
  if (not is_generic(w.meta().quantity())) {
    return std::unexpected{VerticalErrc::not_generic};
  }
  if (auto e = aligned_unit<VerticalErrc>(horizontal.u(), w); not e) {
    return std::unexpected{e.error()};
  }
  const TimeSeries& u = horizontal.u();
  std::vector<Sample> speed(u.size());
  for (std::size_t i = 0; i < speed.size(); ++i) {
    speed[i] =
        hypot3(u.samples()[i], horizontal.v().samples()[i], w.samples()[i]);
  }
  return derived(
      u, std::move(speed),
      SeriesMeta::make(
          {.quantity = GenericQuantity::value(),
           .label =
               "3D " + stem(horizontal.kind(), u.meta().label()) + " speed",
           .unit = horizontal.unit()}));
}

}  // namespace mov::core
