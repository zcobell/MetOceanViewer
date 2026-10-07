// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/core/meta.hpp and mov/core/timeseries.hpp.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

#include "mov/core/meta.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/timeseries.hpp"
#include "test_helpers.hpp"

using mov::core::AtStation;
using mov::core::ConstructionError;
using mov::core::FileStation;
using mov::core::Normalized;
using mov::core::NormalizeReport;
using mov::core::ObsVsPred;
using mov::core::Point;
using mov::core::Sample;
using mov::core::SeriesMeta;
using mov::core::TimeSeries;

namespace {

template <class T>
concept MetaViews = requires(T&& m) {
  std::forward<T>(m).quantity();
  std::forward<T>(m).label();
  std::forward<T>(m).unit();
};

template <class T>
concept SeriesViews = requires(T&& s) {
  std::forward<T>(s).times();
  std::forward<T>(s).samples();
  std::forward<T>(s).meta();
  std::forward<T>(s).points();
};

template <class F>
concept PublicSampleTransform =
    requires(const TimeSeries& s, F f) { s.transform_samples(f); };

template <class F>
concept KeyedSampleTransform =
    requires(const TimeSeries& s, const mov::core::detail::CoreKey& key, F f) {
      s.transform_samples(key, f);
    };

}  // namespace

TEST_CASE("series types are regular and cheap to move",
          "[core][timeseries][constexpr]") {
  STATIC_REQUIRE(std::regular<SeriesMeta>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<SeriesMeta>);
  STATIC_REQUIRE(std::regular<SeriesMeta::Fields>);
  STATIC_REQUIRE(std::regular<TimeSeries>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<TimeSeries>);
  STATIC_REQUIRE(std::is_nothrow_move_assignable_v<TimeSeries>);
  STATIC_REQUIRE(std::regular<Point>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<Point>);
  STATIC_REQUIRE(std::regular<NormalizeReport>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<NormalizeReport>);
  STATIC_REQUIRE(std::regular<ConstructionError>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<ConstructionError>);
  STATIC_REQUIRE(std::regular<mov::core::LengthMismatch>);
  STATIC_REQUIRE(std::regular<mov::core::TimeNotIncreasing>);
  STATIC_REQUIRE(std::regular<Normalized>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Normalized>);
  STATIC_REQUIRE(std::regular<ObsVsPred>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<ObsVsPred>);
  STATIC_REQUIRE(std::copyable<AtStation<FileStation, TimeSeries>>);
  STATIC_REQUIRE(std::equality_comparable<AtStation<FileStation, TimeSeries>>);
  STATIC_REQUIRE(
      std::is_nothrow_move_constructible_v<AtStation<FileStation, TimeSeries>>);
}

TEST_CASE("bypasses need the core passkey", "[core][meta][constexpr]") {
  using mov::core::detail::CoreKey;
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<CoreKey>);
  STATIC_REQUIRE_FALSE(std::is_aggregate_v<CoreKey>);
  // The validated constructor is private: Fields go through make.
  STATIC_REQUIRE_FALSE(std::is_constructible_v<SeriesMeta, SeriesMeta::Fields>);
}

TEST_CASE("views into metadata and series need an lvalue",
          "[core][timeseries][constexpr]") {
  STATIC_REQUIRE(MetaViews<const SeriesMeta&>);
  STATIC_REQUIRE_FALSE(MetaViews<SeriesMeta>);
  STATIC_REQUIRE_FALSE(MetaViews<const SeriesMeta>);
  STATIC_REQUIRE(SeriesViews<const TimeSeries&>);
  STATIC_REQUIRE(SeriesViews<TimeSeries&>);
  STATIC_REQUIRE_FALSE(SeriesViews<TimeSeries>);
  STATIC_REQUIRE_FALSE(SeriesViews<const TimeSeries>);
  // datum() and size() return values, so any series may answer them.
  STATIC_REQUIRE(requires { SeriesMeta{}.datum(); });
  STATIC_REQUIRE(requires { TimeSeries{}.size(); });
}

TEST_CASE("transform_samples is core-only and takes Sample -> Sample",
          "[core][timeseries][constexpr]") {
  STATIC_REQUIRE_FALSE(PublicSampleTransform<Sample (*)(Sample)>);
  STATIC_REQUIRE(KeyedSampleTransform<Sample (*)(Sample)>);
  STATIC_REQUIRE_FALSE(KeyedSampleTransform<double (*)(Sample)>);
  STATIC_REQUIRE_FALSE(KeyedSampleTransform<Sample (*)(double)>);
  STATIC_REQUIRE_FALSE(KeyedSampleTransform<int>);
}

TEST_CASE("a report is clean only when nothing changed",
          "[core][timeseries][constexpr]") {
  STATIC_REQUIRE(NormalizeReport{}.clean());
  STATIC_REQUIRE_FALSE(NormalizeReport{.descents = 1}.clean());
  STATIC_REQUIRE_FALSE(NormalizeReport{.duplicates_dropped = 1}.clean());
  STATIC_REQUIRE_FALSE(
      NormalizeReport{.duplicates_dropped = 2, .conflicting_duplicates = 1}
          .clean());
}

TEST_CASE("first_not_increasing finds the first non-ascent",
          "[core][timeseries][constexpr]") {
  using mov::core::Time;
  using mov::core::detail::first_not_increasing;
  using mov::test::at_ms;
  constexpr std::array<Time, 0> none{};
  constexpr std::array increasing{at_ms(-5), at_ms(0), at_ms(7)};
  constexpr std::array repeat{at_ms(0), at_ms(1), at_ms(1), at_ms(0)};
  constexpr std::array descent{at_ms(3), at_ms(2)};
  STATIC_REQUIRE_FALSE(first_not_increasing(none).has_value());
  STATIC_REQUIRE_FALSE(first_not_increasing(increasing).has_value());
  STATIC_REQUIRE(first_not_increasing(repeat) == std::optional<std::size_t>{2});
  STATIC_REQUIRE(first_not_increasing(descent) ==
                 std::optional<std::size_t>{1});
}
