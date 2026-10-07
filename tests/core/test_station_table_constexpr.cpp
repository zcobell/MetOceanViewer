// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/core/station_table.hpp and the UTF-8 check
// it uses (mov/core/detail/utf8.hpp).

#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

#include "mov/core/detail/utf8.hpp"
#include "mov/core/station_table.hpp"

using mov::core::StationRow;
using mov::core::StationSelection;
using mov::core::StationTable;
using mov::core::TableError;
using mov::core::detail::is_valid_utf8;

namespace {

template <class T>
concept TableViews = requires(T&& t) {
  std::forward<T>(t).schema();
  std::forward<T>(t).station(0);
  std::forward<T>(t).times(0);
  std::forward<T>(t).column(0, 0);
};

template <class T>
concept SelectionView = requires(T&& t) { std::forward<T>(t).indices(); };

}  // namespace

TEST_CASE("table types are values", "[core][station_table][constexpr]") {
  STATIC_REQUIRE(std::regular<StationTable>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<StationTable>);
  STATIC_REQUIRE(std::is_nothrow_move_assignable_v<StationTable>);
  STATIC_REQUIRE(std::copyable<StationRow>);
  STATIC_REQUIRE(std::equality_comparable<StationRow>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<StationRow>);
  STATIC_REQUIRE(std::regular<TableError>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<TableError>);
  // A selection is always stated: no default, built only by make or all.
  STATIC_REQUIRE(std::copyable<StationSelection>);
  STATIC_REQUIRE(std::equality_comparable<StationSelection>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<StationSelection>);
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<StationSelection>);
}

TEST_CASE("views into a table need an lvalue",
          "[core][station_table][constexpr]") {
  STATIC_REQUIRE(TableViews<const StationTable&>);
  STATIC_REQUIRE(TableViews<StationTable&>);
  STATIC_REQUIRE_FALSE(TableViews<StationTable>);
  STATIC_REQUIRE_FALSE(TableViews<const StationTable>);
  // series() copies, so a temporary table may hand it out.
  STATIC_REQUIRE(requires { StationTable{}.series(0, 0); });
  STATIC_REQUIRE(SelectionView<const StationSelection&>);
  STATIC_REQUIRE_FALSE(SelectionView<StationSelection>);
}

TEST_CASE("UTF-8 well-formedness", "[core][station_table][constexpr]") {
  STATIC_REQUIRE(is_valid_utf8(""));
  STATIC_REQUIRE(is_valid_utf8("Grand Isle, LA"));
  STATIC_REQUIRE(is_valid_utf8("\xC3\x89le"));                 // U+00C9
  STATIC_REQUIRE(is_valid_utf8("\xE2\x88\x92"));               // U+2212
  STATIC_REQUIRE(is_valid_utf8("\xED\x9F\xBF"));               // U+D7FF
  STATIC_REQUIRE(is_valid_utf8("\xEE\x80\x80"));               // U+E000
  STATIC_REQUIRE(is_valid_utf8("\xF0\x9F\x8C\x8A"));           // U+1F30A
  STATIC_REQUIRE(is_valid_utf8("\xF4\x8F\xBF\xBF"));           // U+10FFFF
  STATIC_REQUIRE(is_valid_utf8(std::string_view{"a\0b", 3}));  // NUL is UTF-8
  STATIC_REQUIRE_FALSE(is_valid_utf8("\x80"));          // stray continuation
  STATIC_REQUIRE_FALSE(is_valid_utf8("\xC3"));          // truncated
  STATIC_REQUIRE_FALSE(is_valid_utf8("\xC3\x28"));      // bad continuation
  STATIC_REQUIRE_FALSE(is_valid_utf8("\xC0\xAF"));      // overlong
  STATIC_REQUIRE_FALSE(is_valid_utf8("\xE0\x80\xAF"));  // overlong
  STATIC_REQUIRE_FALSE(is_valid_utf8("\xED\xA0\x80"));  // surrogate
  STATIC_REQUIRE_FALSE(is_valid_utf8("\xF0\x80\x80\xAF"));  // overlong
  STATIC_REQUIRE_FALSE(is_valid_utf8("\xF4\x90\x80\x80"));  // > U+10FFFF
  STATIC_REQUIRE_FALSE(is_valid_utf8("\xF5\x80\x80\x80"));
  STATIC_REQUIRE_FALSE(is_valid_utf8("\xE2\x88"));          // truncated
  STATIC_REQUIRE_FALSE(is_valid_utf8("\xF0\x9F\x8C\x28"));  // bad 4th byte
  STATIC_REQUIRE_FALSE(is_valid_utf8("ok\xFF"));
}
