// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <cstddef>
#include <expected>
#include <string>
#include <utility>
#include <vector>

#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/warning.hpp"

namespace {

using mov::io::Read;
using mov::io::Warning;
using mov::io::WarningCode;

Warning warning(WarningCode code, std::string subject = {},
                std::size_t count = 1) {
  return Warning{.code = code, .subject = std::move(subject), .count = count};
}

}  // namespace

TEST_CASE("every WarningCode has its own stable token", "[io][warning]") {
  using mov::io::to_token;
  const std::array<std::pair<WarningCode, std::string_view>, 23> expected{{
      {WarningCode::times_reordered, "times_reordered"},
      {WarningCode::duplicate_times_dropped, "duplicate_times_dropped"},
      {WarningCode::conflicting_duplicate_times, "conflicting_duplicate_times"},
      {WarningCode::legacy_sentinel_masked, "legacy_sentinel_masked"},
      {WarningCode::nonfinite_masked, "nonfinite_masked"},
      {WarningCode::unrecognized_unit, "unrecognized_unit"},
      {WarningCode::partial_record_dropped, "partial_record_dropped"},
      {WarningCode::fewer_snapshots_than_header, "fewer_snapshots_than_header"},
      {WarningCode::epoch_used, "epoch_used"},
      {WarningCode::time_precision_dropped, "time_precision_dropped"},
      {WarningCode::header_line_skipped, "header_line_skipped"},
      {WarningCode::duplicate_station_id_renamed,
       "duplicate_station_id_renamed"},
      {WarningCode::invalid_utf8_replaced, "invalid_utf8_replaced"},
      {WarningCode::foreign_cf, "foreign_cf"},
      {WarningCode::crs_assumed, "crs_assumed"},
      {WarningCode::crs_approximate, "crs_approximate"},
      {WarningCode::datum_unknown, "datum_unknown"},
      {WarningCode::tz_assumed_utc, "tz_assumed_utc"},
      {WarningCode::skipped_variable, "skipped_variable"},
      {WarningCode::minor_newer, "minor_newer"},
      {WarningCode::unknown_provider, "unknown_provider"},
      {WarningCode::unknown_quantity, "unknown_quantity"},
      {WarningCode::legacy_dialect, "legacy_dialect"},
  }};
  for (const auto& [code, token] : expected) {
    CHECK(to_token(code) == token);
  }
}

TEST_CASE("Warning defaults to a count of one and compares by value",
          "[io][warning]") {
  const Warning w{.code = WarningCode::epoch_used};
  CHECK(w.count == 1);
  CHECK(w.subject.empty());
  CHECK(w == warning(WarningCode::epoch_used));
  CHECK(w != warning(WarningCode::epoch_used, "", 2));
  CHECK(w != warning(WarningCode::crs_assumed));
  CHECK(w != warning(WarningCode::epoch_used, "x"));
}

TEST_CASE("Read::transform maps the value and keeps the warnings",
          "[io][read]") {
  Read<int> r{.value = 20, .warnings = {warning(WarningCode::epoch_used)}};
  const Read<std::string> mapped =
      std::move(r).transform([](int v) { return std::to_string(v + 1); });
  CHECK(mapped.value == "21");
  CHECK(mapped.warnings == std::vector{warning(WarningCode::epoch_used)});
}

namespace {

// A value that can only be moved.
struct MoveOnly {
  explicit MoveOnly(int v) : value{v} {}
  MoveOnly(const MoveOnly&) = delete;
  MoveOnly& operator=(const MoveOnly&) = delete;
  MoveOnly(MoveOnly&&) noexcept = default;
  MoveOnly& operator=(MoveOnly&&) noexcept = default;
  ~MoveOnly() = default;
  int value;
};

}  // namespace

TEST_CASE("Read::transform passes the value on by move", "[io][read]") {
  Read<MoveOnly> r{.value = MoveOnly{5}, .warnings = {}};
  const Read<int> mapped =
      std::move(r).transform([](MoveOnly p) { return p.value * 2; });
  CHECK(mapped.value == 10);
}

TEST_CASE("Read::and_then concatenates warnings, the first stage's first",
          "[io][read]") {
  Read<int> first{.value = 2,
                  .warnings = {warning(WarningCode::times_reordered),
                               warning(WarningCode::crs_assumed, "a")}};
  const Read<int> second = std::move(first).and_then([](int v) {
    return Read<int>{.value = v * 10,
                     .warnings = {warning(WarningCode::epoch_used)}};
  });
  CHECK(second.value == 20);
  CHECK(second.warnings == std::vector{warning(WarningCode::times_reordered),
                                       warning(WarningCode::crs_assumed, "a"),
                                       warning(WarningCode::epoch_used)});
}

TEST_CASE("Read::and_then can change the value type and end without warnings",
          "[io][read]") {
  Read<int> first{.value = 3, .warnings = {}};
  const Read<std::string> second = std::move(first).and_then([](int v) {
    return Read<std::string>{
        .value = std::string(static_cast<std::size_t>(v), 'x'), .warnings = {}};
  });
  CHECK(second.value == "xxx");
  CHECK(second.warnings.empty());
}

TEST_CASE("Read chains of three stages keep the order", "[io][read]") {
  Read<int> a{.value = 1, .warnings = {warning(WarningCode::minor_newer)}};
  const Read<int> out =
      std::move(a)
          .and_then([](int v) {
            return Read<int>{.value = v + 1,
                             .warnings = {warning(WarningCode::foreign_cf)}};
          })
          .transform([](int v) { return v + 1; })
          .and_then([](int v) {
            return Read<int>{
                .value = v + 1,
                .warnings = {warning(WarningCode::legacy_dialect)}};
          });
  CHECK(out.value == 4);
  CHECK(out.warnings == std::vector{warning(WarningCode::minor_newer),
                                    warning(WarningCode::foreign_cf),
                                    warning(WarningCode::legacy_dialect)});
}

TEST_CASE("and_then_read continues a success and joins the warnings",
          "[io][read]") {
  std::expected<Read<int>, std::string> first =
      Read<int>{.value = 4, .warnings = {warning(WarningCode::epoch_used)}};
  const auto out = mov::io::and_then_read(
      std::move(first), [](int v) -> std::expected<Read<int>, std::string> {
        return Read<int>{.value = v * v,
                         .warnings = {warning(WarningCode::crs_assumed)}};
      });
  REQUIRE(out.has_value());
  CHECK(out->value == 16);
  CHECK(out->warnings == std::vector{warning(WarningCode::epoch_used),
                                     warning(WarningCode::crs_assumed)});
}

TEST_CASE("and_then_read returns the first stage's error without calling on",
          "[io][read]") {
  std::expected<Read<int>, std::string> failed = std::unexpected{"boom"};
  bool called = false;
  const auto out = mov::io::and_then_read(
      std::move(failed),
      [&called](int) -> std::expected<Read<int>, std::string> {
        called = true;
        return Read<int>{.value = 0, .warnings = {}};
      });
  REQUIRE(not out.has_value());
  CHECK(out.error() == "boom");
  CHECK(not called);
}

TEST_CASE("and_then_read converts the continuation's error type",
          "[io][read]") {
  struct Narrow {
    int code;
  };
  struct Wide {
    Wide(Narrow n)
        : code{n.code + 100} {}  // NOLINT(google-explicit-constructor)
    explicit Wide(int c) : code{c} {}
    int code;
  };
  std::expected<Read<int>, Wide> first = Read<int>{.value = 1, .warnings = {}};
  const std::expected<Read<int>, Wide> out = mov::io::and_then_read(
      std::move(first), [](int) -> std::expected<Read<int>, Narrow> {
        return std::unexpected{Narrow{.code = 7}};
      });
  REQUIRE(not out.has_value());
  CHECK(out.error().code == 107);
}

TEST_CASE("and_then_read result types are exact", "[io][read]") {
  using Result = std::expected<Read<std::string>, int>;
  auto continuation = [](int v) -> Result {
    return Read<std::string>{.value = std::to_string(v), .warnings = {}};
  };
  STATIC_REQUIRE(std::same_as<decltype(mov::io::and_then_read(
                                  std::declval<std::expected<Read<int>, int>>(),
                                  continuation)),
                              Result>);
  const Result out = mov::io::and_then_read(
      std::expected<Read<int>, int>{Read<int>{.value = 42, .warnings = {}}},
      continuation);
  REQUIRE(out.has_value());
  CHECK(out->value == "42");
}

// ---- the monad laws ---------------------------------------------------------

namespace {

Read<int> add_one(int v) {
  return Read<int>{.value = v + 1,
                   .warnings = {warning(WarningCode::foreign_cf, "add_one")}};
}

Read<int> double_it(int v) {
  return Read<int>{.value = v * 2,
                   .warnings = {warning(WarningCode::crs_assumed, "double")}};
}

Read<int> start() {
  return Read<int>{.value = 5, .warnings = {warning(WarningCode::epoch_used)}};
}

using Fallible = std::expected<Read<int>, std::string>;

Fallible fallible_add_one(int v) { return add_one(v); }

Fallible fallible_double(int v) { return double_it(v); }

Fallible fails(int) { return std::unexpected{"stop"}; }

Fallible fallible_start() { return start(); }

}  // namespace

TEST_CASE("Read: pure is a left and right identity of and_then",
          "[io][read][laws]") {
  // pure(x).and_then(f) == f(x)
  CHECK(mov::io::pure(5).and_then(add_one) == add_one(5));
  // m.and_then(pure) == m
  CHECK(start().and_then([](int v) { return mov::io::pure(v); }) == start());
  CHECK(mov::io::pure(5) == Read<int>{.value = 5, .warnings = {}});
}

TEST_CASE("Read: and_then is associative", "[io][read][laws]") {
  const Read<int> left = start().and_then(add_one).and_then(double_it);
  const Read<int> right =
      start().and_then([](int v) { return add_one(v).and_then(double_it); });
  CHECK(left == right);
  CHECK(left.value == 12);
  CHECK(left.warnings.size() == 3);
}

TEST_CASE("Read: transform is and_then of pure", "[io][read][laws]") {
  const auto twice = [](int v) { return v * 2; };
  CHECK(start().transform(twice) ==
        start().and_then([&twice](int v) { return mov::io::pure(twice(v)); }));
}

TEST_CASE("and_then_read: pure is a left and right identity",
          "[io][read][laws]") {
  const auto lift_pure = [](int v) -> Fallible { return mov::io::pure(v); };
  CHECK(mov::io::and_then_read(Fallible{mov::io::pure(5)}, fallible_add_one) ==
        fallible_add_one(5));
  CHECK(mov::io::and_then_read(fallible_start(), lift_pure) ==
        fallible_start());
}

TEST_CASE("and_then_read is associative", "[io][read][laws]") {
  const Fallible left = mov::io::and_then_read(
      mov::io::and_then_read(fallible_start(), fallible_add_one),
      fallible_double);
  const Fallible right =
      mov::io::and_then_read(fallible_start(), [](int v) -> Fallible {
        return mov::io::and_then_read(fallible_add_one(v), fallible_double);
      });
  REQUIRE(left.has_value());
  CHECK(left == right);
  CHECK(left->value == 12);
}

TEST_CASE("and_then_read: an error anywhere short-circuits and drops warnings",
          "[io][read][laws]") {
  // An error in the middle of a chain: nothing after it runs.
  bool ran_after = false;
  const Fallible out =
      mov::io::and_then_read(mov::io::and_then_read(fallible_start(), fails),
                             [&ran_after](int v) -> Fallible {
                               ran_after = true;
                               return add_one(v);
                             });
  REQUIRE(not out.has_value());
  CHECK(out.error() == "stop");
  CHECK(not ran_after);

  // The error is a left zero and a right zero.
  const Fallible zero_left = mov::io::and_then_read(
      Fallible{std::unexpected{"first"}}, fallible_add_one);
  CHECK(zero_left == Fallible{std::unexpected{"first"}});
  const Fallible zero_right = mov::io::and_then_read(fallible_start(), fails);
  CHECK(zero_right == Fallible{std::unexpected{"stop"}});
}
