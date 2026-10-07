// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/core/meta.hpp and mov/core/timeseries.hpp.

#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <type_traits>
#include <utility>

#include "mov/core/meta.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/timeseries.hpp"

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
concept SampleTransform =
    requires(const TimeSeries& s, F f) { s.transform_samples(f); };

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
  STATIC_REQUIRE(std::regular<Normalized>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Normalized>);
  STATIC_REQUIRE(std::regular<ObsVsPred>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<ObsVsPred>);
  STATIC_REQUIRE(std::copyable<AtStation<FileStation, TimeSeries>>);
  STATIC_REQUIRE(std::equality_comparable<AtStation<FileStation, TimeSeries>>);
  STATIC_REQUIRE(
      std::is_nothrow_move_constructible_v<AtStation<FileStation, TimeSeries>>);
}

TEST_CASE("an engaged unit or datum is replaced only with the passkey",
          "[core][meta][constexpr]") {
  STATIC_REQUIRE_FALSE(
      std::is_default_constructible_v<mov::core::detail::MetaRewrite>);
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

TEST_CASE("transform_samples takes Sample -> Sample only",
          "[core][timeseries][constexpr]") {
  STATIC_REQUIRE(SampleTransform<Sample (*)(Sample)>);
  STATIC_REQUIRE_FALSE(SampleTransform<double (*)(Sample)>);
  STATIC_REQUIRE_FALSE(SampleTransform<Sample (*)(double)>);
  STATIC_REQUIRE_FALSE(SampleTransform<int>);
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
