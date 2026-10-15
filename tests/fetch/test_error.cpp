// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// fetch's error type, results and the trail between stages
// (docs/providers-design.md §5.1).

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <type_traits>
#include <variant>
#include <vector>

#include "mov/core/quantity.hpp"
#include "mov/fetch/error.hpp"
#include "mov/fetch/part.hpp"

namespace {

using namespace mov::fetch;

template <class T, class V>
constexpr bool holds_type = false;
template <class T, class... A>
constexpr bool holds_type<T, std::variant<A...>> =
    (std::is_same_v<T, A> or ...);

// Every io::Error alternative is a fetch::Error alternative, once.
template <class... A>
constexpr bool all_in_error(std::variant<A...>* /*tag*/) {
  return (holds_type<A, Error> and ...);
}
static_assert(all_in_error(static_cast<mov::io::Error*>(nullptr)));
static_assert(std::variant_size_v<Error> ==
              std::variant_size_v<mov::io::Error> + 17);
static_assert(detail::unique_alternatives<Error>);
static_assert(not detail::unique_alternatives<std::variant<int, int>>);

const ColumnKey key{.quantity = mov::core::Quantity::water_level};

Trail trail(std::uint32_t exchanges, mov::io::WarningCode code) {
  return {.warnings = {mov::io::Warning{.code = code}},
          .stats = {.exchanges = exchanges, .retries = 0, .bytes = exchanges}};
}

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
  CHECK(lift_merge(MetaMismatch{.column = key}) ==
        Error{MetaMismatch{.column = key}});
  CHECK(lift_merge(UnitMismatch{.column = key}) ==
        Error{UnitMismatch{.column = key}});
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
  // Surrounding white space is trimmed first (as io::wire's).
  const auto padded =
      ServiceError::make(Origin::usgs, ServiceErrc::unknown, " \tNo key \n");
  CHECK(padded.text() == "No key");
}

TEST_CASE("the trail is a monoid: warnings in order, stats summed",
          "[fetch][stages]") {
  const Trail a = trail(2, mov::io::WarningCode::epoch_used);
  const Trail b = trail(3, mov::io::WarningCode::times_reordered);
  const Trail c = trail(5, mov::io::WarningCode::foreign_cf);
  CHECK(a + Trail{} == a);
  CHECK(Trail{} + a == a);
  CHECK((a + b) + c == a + (b + c));
  const Trail ab = a + b;
  CHECK(ab.stats == Stats{.exchanges = 5, .retries = 0, .bytes = 5});
  REQUIRE(ab.warnings.size() == 2);
  CHECK(std::get<mov::io::Warning>(ab.warnings[0]).code ==
        mov::io::WarningCode::epoch_used);
  const Fetched<int> f{.value = 7, .trail = ab};
  CHECK(f.value == 7);
}
