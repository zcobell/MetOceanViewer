// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/timeseries.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <iterator>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace mov::core {

namespace {

std::pair<TimeAxis, std::vector<Sample>> unzip(std::span<const Point> rows) {
  TimeAxis times(rows.size());
  std::vector<Sample> samples(rows.size());
  std::ranges::transform(rows, times.begin(), &Point::time);
  std::ranges::transform(rows, samples.begin(), &Point::sample);
  return {std::move(times), std::move(samples)};
}

}  // namespace

std::expected<TimeSeries, ConstructionError> TimeSeries::make(
    TimeAxis times, std::vector<Sample> samples, SeriesMeta meta) {
  if (times.size() != samples.size()) {
    return std::unexpected{ConstructionError{
        LengthMismatch{.times = times.size(), .samples = samples.size()}}};
  }
  if (const auto bad = detail::first_not_increasing(times)) {
    return std::unexpected{ConstructionError{TimeNotIncreasing{.index = *bad}}};
  }
  return TimeSeries{std::move(times), std::move(samples), std::move(meta)};
}

TimeSeries TimeSeries::with_label(std::string label) const& {
  TimeSeries copy = *this;
  return std::move(copy).with_label(std::move(label));
}

TimeSeries TimeSeries::with_label(std::string label) && {
  meta_ = std::move(meta_).with_label(std::move(label));
  return std::move(*this);
}

std::expected<TimeSeries, AssumeUnitError> TimeSeries::assume_unit(
    Unit u) const& {
  return meta_.assume_unit(std::move(u)).transform([this](SeriesMeta m) {
    return TimeSeries{times_, samples_, std::move(m)};
  });
}

std::expected<TimeSeries, AssumeUnitError> TimeSeries::assume_unit(Unit u) && {
  return meta_.assume_unit(std::move(u)).transform([this](SeriesMeta m) {
    meta_ = std::move(m);
    return std::move(*this);
  });
}

std::expected<TimeSeries, AssumeDatumError> TimeSeries::assume_datum(
    VerticalDatum d) const& {
  return meta_.assume_datum(d).transform([this](SeriesMeta m) {
    return TimeSeries{times_, samples_, std::move(m)};
  });
}

std::expected<TimeSeries, AssumeDatumError> TimeSeries::assume_datum(
    VerticalDatum d) && {
  return meta_.assume_datum(d).transform([this](SeriesMeta m) {
    meta_ = std::move(m);
    return std::move(*this);
  });
}

std::expected<TimeSeries, LengthMismatch> TimeSeries::with_samples(
    std::vector<Sample> samples, SeriesMeta meta) const& {
  if (samples.size() != size()) {
    return std::unexpected{
        LengthMismatch{.times = size(), .samples = samples.size()}};
  }
  return TimeSeries{times_, std::move(samples), std::move(meta)};
}

std::expected<TimeSeries, LengthMismatch> TimeSeries::with_samples(
    std::vector<Sample> samples, SeriesMeta meta) && {
  if (samples.size() != size()) {
    return std::unexpected{
        LengthMismatch{.times = size(), .samples = samples.size()}};
  }
  return TimeSeries{std::move(times_), std::move(samples), std::move(meta)};
}

Normalized normalize(std::vector<Point> rows, SeriesMeta meta) {
  TimeAxis input_times(rows.size());
  std::ranges::transform(rows, input_times.begin(), &Point::time);
  const NormalizingOrder order = normalizing_order(
      input_times, [&rows](std::size_t kept, std::size_t dropped) {
        return rows[kept].sample != rows[dropped].sample;
      });
  if (not order.report.clean()) {
    std::vector<Point> kept_rows;
    kept_rows.reserve(order.kept.size());
    std::ranges::transform(order.kept, std::back_inserter(kept_rows),
                           [&rows](std::size_t i) { return rows[i]; });
    rows = std::move(kept_rows);
  }
  auto [times, samples] = unzip(rows);
  return {.series =
              TimeSeries{std::move(times), std::move(samples), std::move(meta)},
          .report = order.report};
}

}  // namespace mov::core
