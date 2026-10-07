// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks of the netCDF wrapper's value types: names, sizes,
// the read type table and masking.

#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

#include "mov/core/sample.hpp"
#include "mov/io/detail/checked_product.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/netcdf/masking.hpp"
#include "mov/io/netcdf/name.hpp"
#include "mov/io/netcdf/types.hpp"

namespace {

using mov::core::Missing;
using mov::core::Sample;
using mov::io::detail::checked_product;
using mov::io::nc::Masking;
using mov::io::nc::NcName;
using mov::io::nc::NcNameError;
using mov::io::nc::NcNameRef;
using mov::io::nc::readable_as;
using mov::io::nc::Type;
using mov::io::nc::type_of;

constexpr std::size_t size_max = std::numeric_limits<std::size_t>::max();

constexpr std::optional<std::size_t> product(
    std::initializer_list<std::size_t> factors) {
  return checked_product(std::span{factors.begin(), factors.size()});
}

constexpr Sample value(double v) { return mov::core::finite_or_missing(v); }

// A NewFile cannot be created from outside write_netcdf_atomic.
template <class F>
concept CanCreate =
    requires(const std::filesystem::path& p) { F::create(p, p); };

}  // namespace

TEST_CASE("checked_product", "[io][netcdf][constexpr]") {
  STATIC_REQUIRE(product({}) == 1U);
  STATIC_REQUIRE(product({2, 3, 4}) == 24U);
  STATIC_REQUIRE(product({size_max, 1}) == size_max);
  STATIC_REQUIRE(product({size_max, 2}) == std::nullopt);
  STATIC_REQUIRE(product({std::size_t{1} << 32U, std::size_t{1} << 32U}) ==
                 std::nullopt);
  STATIC_REQUIRE(product({std::size_t{1} << 31U, std::size_t{1} << 31U}) ==
                 std::size_t{1} << 62U);
  // A zero factor is an empty hyperslab, not an overflow.
  STATIC_REQUIRE(product({size_max, size_max, 0}) == 0U);
  STATIC_REQUIRE(product({0, size_max, size_max}) == 0U);
}

TEST_CASE("netCDF name rules", "[io][netcdf][constexpr]") {
  using mov::io::nc::detail::check_name;
  STATIC_REQUIRE(check_name("time").has_value());
  STATIC_REQUIRE(check_name("") == std::unexpected{NcNameError::empty});
  STATIC_REQUIRE(check_name(std::string_view{"a\0b", 3}) ==
                 std::unexpected{NcNameError::embedded_nul});
  constexpr std::array<char, 257> long_name = [] {
    std::array<char, 257> a{};
    a.fill('x');
    return a;
  }();
  STATIC_REQUIRE(check_name(std::string_view{long_name.data(), 256}));
  STATIC_REQUIRE(check_name(std::string_view{long_name.data(), 257}) ==
                 std::unexpected{NcNameError::too_long});
}

TEST_CASE("NcNameRef literals are checked at compile time",
          "[io][netcdf][constexpr]") {
  constexpr NcNameRef time{"time"};
  STATIC_REQUIRE(time.view() == "time");
  STATIC_REQUIRE(time.c_str()[4] == '\0');
  STATIC_REQUIRE(time == NcNameRef{"time"});
  STATIC_REQUIRE(not(time == NcNameRef{"tim"}));
  // A temporary NcName cannot be viewed; a default NcNameRef does not exist.
  STATIC_REQUIRE(std::constructible_from<NcNameRef, const NcName&>);
  STATIC_REQUIRE(not std::constructible_from<NcNameRef, NcName&&>);
  STATIC_REQUIRE(not std::is_default_constructible_v<NcNameRef>);
  STATIC_REQUIRE(not std::is_default_constructible_v<NcName>);
  STATIC_REQUIRE((std::copyable<NcName> and std::equality_comparable<NcName>));
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<NcName>);
  STATIC_REQUIRE(
      (std::copyable<NcNameRef> and std::equality_comparable<NcNameRef>));
}

TEST_CASE("type_of and the read table of section 4.3",
          "[io][netcdf][constexpr]") {
  STATIC_REQUIRE(type_of<double> == Type::double_);
  STATIC_REQUIRE(type_of<float> == Type::float_);
  STATIC_REQUIRE(type_of<std::int8_t> == Type::byte);
  STATIC_REQUIRE(type_of<std::int16_t> == Type::short_);
  STATIC_REQUIRE(type_of<std::int32_t> == Type::int_);
  STATIC_REQUIRE(type_of<std::int64_t> == Type::int64);
  STATIC_REQUIRE(not mov::io::nc::Numeric<unsigned>);
  STATIC_REQUIRE(not mov::io::nc::Numeric<char>);
  STATIC_REQUIRE(not mov::io::nc::Numeric<long double>);

  // Rows: the variable's type; columns: double float int8 int16 int32 int64.
  constexpr auto row = [](Type from) {
    return std::array{
        readable_as<double>(from),       readable_as<float>(from),
        readable_as<std::int8_t>(from),  readable_as<std::int16_t>(from),
        readable_as<std::int32_t>(from), readable_as<std::int64_t>(from)};
  };
  using R = std::array<bool, 6>;
  STATIC_REQUIRE(row(Type::double_) ==
                 R{true, false, false, false, false, false});
  STATIC_REQUIRE(row(Type::float_) ==
                 R{true, true, false, false, false, false});
  STATIC_REQUIRE(row(Type::byte) == R{true, false, true, true, true, true});
  STATIC_REQUIRE(row(Type::short_) == R{true, false, false, true, true, true});
  STATIC_REQUIRE(row(Type::int_) == R{true, false, false, false, true, true});
  STATIC_REQUIRE(row(Type::int64) == R{true, false, false, false, false, true});
  constexpr R none{};
  STATIC_REQUIRE(row(Type::ubyte) == none);
  STATIC_REQUIRE(row(Type::char_) == none);
  STATIC_REQUIRE(row(Type::ushort) == none);
  STATIC_REQUIRE(row(Type::uint) == none);
  STATIC_REQUIRE(row(Type::uint64) == none);
  STATIC_REQUIRE(row(Type::string) == none);
  STATIC_REQUIRE(row(Type::other) == none);
}

// The Masking objects below are not const: GCC 14 rejects destroying a const
// std::vector member during constant evaluation.
TEST_CASE("Masking compares in the variable's own type",
          "[io][netcdf][constexpr]") {
  // B4: the float fill -99999f masks the float -99999f, and NaN is missing.
  STATIC_REQUIRE([] {
    // NOLINTNEXTLINE(misc-const-correctness): GCC 14, see above
    Masking<float> m{.fill = -99999.0F};
    return m.apply(-99999.0F) == Sample{Missing{}} and
           m.apply(std::numeric_limits<float>::quiet_NaN()) ==
               Sample{Missing{}} and
           m.apply(std::numeric_limits<float>::infinity()) ==
               Sample{Missing{}} and
           m.apply(1.5F) == value(1.5);
  }());
  // B9: the default double fill is a fill, not a value.
  STATIC_REQUIRE([] {
    // NOLINTNEXTLINE(misc-const-correctness): GCC 14, see above
    Masking<double> m{.fill = 9.9692099683868690e+36};
    return m.apply(9.9692099683868690e+36) == Sample{Missing{}} and
           m.apply(9.9692099683868e+36) == value(9.9692099683868e+36);
  }());
  // missing_value as a vector; valid range; values at the bounds are kept.
  STATIC_REQUIRE([] {
    // NOLINTNEXTLINE(misc-const-correctness): GCC 14, see above
    Masking<double> m{.missing_values = {-999.0, -888.0},
                      .valid_min = 0.0,
                      .valid_max = 10.0};
    return m.apply(-999.0) == Sample{Missing{}} and
           m.apply(-888.0) == Sample{Missing{}} and
           m.apply(-1.0) == Sample{Missing{}} and
           m.apply(11.0) == Sample{Missing{}} and m.apply(0.0) == value(0) and
           m.apply(10.0) == value(10);
  }());
  // Unpacking happens after masking, on the widened value.
  STATIC_REQUIRE([] {
    // NOLINTNEXTLINE(misc-const-correctness): GCC 14, see above
    Masking<std::int16_t> m{
        .fill = std::int16_t{-32767}, .scale = 0.5, .offset = 10.0};
    return m.apply(-32767) == Sample{Missing{}} and m.apply(2) == value(11) and
           m.apply(-20) == value(0);
  }());
  // An absent factor is not applied, so -0.0 keeps its sign.
  STATIC_REQUIRE([] {
    // NOLINTNEXTLINE(misc-const-correctness): GCC 14, see above
    Masking<double> plain{};
    const double zero = plain.apply(-0.0).value().value_or(1.0);
    return zero == 0.0 and (std::bit_cast<std::uint64_t>(zero) >> 63U) == 1U;
  }());
  STATIC_REQUIRE((std::regular<Masking<double>> and
                  std::is_nothrow_move_constructible_v<Masking<double>>));
}

TEST_CASE("value types of the wrapper", "[io][netcdf][constexpr]") {
  using mov::io::nc::AttTarget;
  using mov::io::nc::DimRange;
  using mov::io::nc::File;
  using mov::io::nc::Global;
  STATIC_REQUIRE((std::regular<DimRange> and
                  std::is_nothrow_move_constructible_v<DimRange>));
  STATIC_REQUIRE(std::regular<Global>);
  STATIC_REQUIRE(AttTarget{"zeta"}.variable() == NcNameRef{"zeta"});
  STATIC_REQUIRE(AttTarget{mov::io::nc::global}.variable() == std::nullopt);
  STATIC_REQUIRE(not std::constructible_from<AttTarget, NcName&&>);
  STATIC_REQUIRE(not std::copyable<File>);
  STATIC_REQUIRE((std::is_nothrow_move_constructible_v<File> and
                  std::is_nothrow_move_assignable_v<File>));
  using mov::io::nc::NewFile;
  STATIC_REQUIRE(not CanCreate<NewFile>);
  STATIC_REQUIRE(
      not std::constructible_from<NewFile, int, std::filesystem::path,
                                  mov::io::ReadLimits>);
}

TEST_CASE("exact_from converts only values the target holds exactly",
          "[io][netcdf][constexpr]") {
  using mov::io::nc::detail::exact_from;
  constexpr std::int64_t two53 = std::int64_t{1} << 53;
  // From int64.
  STATIC_REQUIRE(exact_from<std::int8_t>(std::int64_t{-128}) == -128);
  STATIC_REQUIRE(exact_from<std::int8_t>(std::int64_t{128}) == std::nullopt);
  STATIC_REQUIRE(exact_from<std::int16_t>(std::int64_t{-999}) == -999);
  STATIC_REQUIRE(exact_from<std::int32_t>(std::int64_t{1} << 40) ==
                 std::nullopt);
  STATIC_REQUIRE(exact_from<std::int64_t>(two53 + 1) == two53 + 1);
  STATIC_REQUIRE(exact_from<double>(two53) == 9007199254740992.0);
  STATIC_REQUIRE(exact_from<double>(two53 + 1) == std::nullopt);
  STATIC_REQUIRE(exact_from<float>(std::int64_t{16777216}) == 16777216.0F);
  STATIC_REQUIRE(exact_from<float>(std::int64_t{16777217}) == std::nullopt);
  STATIC_REQUIRE(exact_from<float>(std::numeric_limits<std::int64_t>::max()) ==
                 std::nullopt);  // rounds to 2^63, beyond int64
  STATIC_REQUIRE(exact_from<double>(std::numeric_limits<std::int64_t>::min()) ==
                 -9223372036854775808.0);
  // From double.
  STATIC_REQUIRE(exact_from<double>(0.1) == 0.1);
  STATIC_REQUIRE(exact_from<float>(-999.0) == -999.0F);
  STATIC_REQUIRE(exact_from<float>(0.1) == std::nullopt);
  STATIC_REQUIRE(exact_from<float>(1e300) == std::nullopt);
  STATIC_REQUIRE(exact_from<float>(std::numeric_limits<double>::infinity()) ==
                 std::numeric_limits<float>::infinity());
  STATIC_REQUIRE(exact_from<std::int32_t>(-999.0) == -999);
  STATIC_REQUIRE(exact_from<std::int32_t>(0.5) == std::nullopt);
  STATIC_REQUIRE(exact_from<std::int8_t>(-128.0) == -128);
  STATIC_REQUIRE(exact_from<std::int8_t>(128.0) == std::nullopt);
  STATIC_REQUIRE(exact_from<std::int64_t>(9223372036854775808.0) ==
                 std::nullopt);
  STATIC_REQUIRE(exact_from<std::int64_t>(-9223372036854775808.0) ==
                 std::numeric_limits<std::int64_t>::min());
  STATIC_REQUIRE(exact_from<std::int16_t>(
                     std::numeric_limits<double>::infinity()) == std::nullopt);
  // NaN is exact in float, and in no integer.
  STATIC_REQUIRE([] {
    const std::optional<float> f =
        exact_from<float>(std::numeric_limits<double>::quiet_NaN());
    return f.has_value() and *f != *f;
  }());
  STATIC_REQUIRE(exact_from<std::int32_t>(
                     std::numeric_limits<double>::quiet_NaN()) == std::nullopt);
}
