// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// fetch's error type, results and the trail between stages
// (docs/providers-design.md §5.1).

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <system_error>
#include <type_traits>
#include <variant>
#include <vector>

#include "mov/fetch/error.hpp"
#include "mov/fetch/part.hpp"

namespace {

using namespace mov::fetch;

template <class T, class V>
constexpr bool holds_type = false;
template <class T, class... A>
constexpr bool holds_type<T, std::variant<A...>> =
    (std::is_same_v<T, A> or ...);

// Every io::Error alternative is a fetch::Error alternative.
template <class... A>
constexpr bool all_in_error(std::variant<A...>* /*tag*/) {
  return (holds_type<A, Error> and ...);
}
static_assert(all_in_error(static_cast<mov::io::Error*>(nullptr)));
static_assert(std::variant_size_v<Error> ==
              std::variant_size_v<mov::io::Error> + 11);

}  // namespace

TEST_CASE("lift_io keeps the io error as it is", "[fetch][error]") {
  const mov::io::ParseError parse =
      mov::io::ParseError::make(mov::io::ParseErrc::bad_date, {.line = 2}, "x");
  CHECK(lift_io(parse) == Error{parse});
  CHECK(lift_io(mov::core::Cancelled{}) == Error{mov::core::Cancelled{}});
  const mov::io::FileError file{
      .op = mov::io::FileOp::open,
      .path = "a",
      .ec = std::make_error_code(std::errc::io_error)};
  CHECK(lift_io(file) == Error{file});
}

TEST_CASE("lift_merge keeps the merge error as it is", "[fetch][error]") {
  CHECK(lift_merge(MetaMismatch{.column = "c"}) ==
        Error{MetaMismatch{.column = "c"}});
  CHECK(lift_merge(UnitMismatch{.column = "c"}) ==
        Error{UnitMismatch{.column = "c"}});
}

TEST_CASE("a service error's text is cut to 120 bytes on a UTF-8 boundary",
          "[fetch][error]") {
  const std::string long_text(200, 'x');
  const auto e =
      ServiceError::make(Origin::coops_data, ServiceErrc::unknown, long_text);
  CHECK(e.text().size() == ServiceError::max_text_bytes);
  CHECK(e.origin() == Origin::coops_data);
  CHECK(e.code() == ServiceErrc::unknown);
  // A two-byte character straddling the cut is dropped whole.
  const std::string straddle = std::string(119, 'x') + "\xC3\xA9";
  const auto cut =
      ServiceError::make(Origin::usgs, ServiceErrc::unknown, straddle);
  CHECK(cut.text() == std::string(119, 'x'));
}

TEST_CASE("split and after carry an earlier stage's trail to the later result",
          "[fetch][stages]") {
  Fetched<int> first{
      .value = 7,
      .warnings = {FetchWarning{CooledDown{
          .origin = Origin::usgs, .for_ = std::chrono::milliseconds{5}}}},
      .stats = {.exchanges = 2, .retries = 1, .bytes = 10}};
  auto [value, trail] = split(std::move(first));
  CHECK(value == 7);
  CHECK(trail.stats == Stats{.exchanges = 2, .retries = 1, .bytes = 10});
  Fetched<std::string> second{
      .value = "data",
      .warnings = {mov::io::Warning{.code = mov::io::WarningCode::epoch_used}},
      .stats = {.exchanges = 3, .retries = 0, .bytes = 5}};
  const Fetched<std::string> joined =
      after(std::move(trail), std::move(second));
  CHECK(joined.value == "data");
  CHECK(joined.stats == Stats{.exchanges = 5, .retries = 1, .bytes = 15});
  REQUIRE(joined.warnings.size() == 2);
  CHECK(std::holds_alternative<FetchWarning>(joined.warnings[0]));
  CHECK(std::holds_alternative<mov::io::Warning>(joined.warnings[1]));
}
