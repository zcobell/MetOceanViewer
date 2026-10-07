// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Every typed entry point of nc::File, for every Numeric type: a file is
// written through write_netcdf_atomic and read back, and each check of the
// typed calls is made once per type.

#include <array>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include "mov/core/sample.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "nc_test_helpers.hpp"

namespace {

using namespace std::literals;
using mov::core::Missing;
using mov::core::Sample;
using mov::io::Error;
using mov::io::nc::DimInfo;
using mov::io::nc::File;
using mov::io::nc::global;
using mov::io::nc::Masking;
using mov::io::nc::NcNameRef;
using mov::io::nc::Slab;
using mov::io::nc::VarOptions;
using mov::io::nc::write_netcdf_atomic;
using mov::test::ScratchDir;
using mov::test::nc::error_of;
using mov::test::nc::LibraryStatus;
using mov::test::nc::NcStatus;
using mov::test::nc::open;
using mov::test::nc::ReadContext;
using mov::test::nc::ReadLimits;
using mov::test::nc::status_of;
using mov::test::nc::value_of;
using mov::test::nc::WrapperFault;

constexpr int nc_enotvar = -49;
const Slab all4{{.start = 0, .count = 4}};

// A type that is not T, for attributes of the wrong type.
template <class T>
using Other = std::conditional_t<std::same_as<T, double>, float, double>;

template <class T>
constexpr T fill_value = T{-99};

template <class T>
std::vector<T> values(std::initializer_list<int> list) {
  std::vector<T> out;
  for (const int v : list) {
    out.push_back(static_cast<T>(v));
  }
  return out;
}

template <class T>
std::expected<void, mov::io::NcError> put(File& f, NcNameRef var,
                                          std::initializer_list<int> list) {
  const std::vector<T> data = values<T>(list);
  return f.put<T>(var, data, all4);
}

template <class T>
std::expected<void, mov::io::NcError> att(File& f, mov::io::nc::AttTarget on,
                                          NcNameRef name,
                                          std::initializer_list<int> list) {
  const std::vector<T> data = values<T>(list);
  return f.put_att<T>(on, name, data);
}

// The checks of the typed write calls, made inside the body.
template <class T>
void check_write_errors(File& f, const std::array<DimInfo, 1>& n,
                        const std::array<DimInfo, 2>& grid) {
  const std::vector<T> four = values<T>({1, 2, 3, 4});
  const std::vector<T> two = values<T>({1, 2});
  CHECK(error_of(f.put<T>("other", four, all4)).status ==
        NcStatus{WrapperFault::type_mismatch});
  CHECK(error_of(f.put<T>("v", two, all4)).status ==
        NcStatus{WrapperFault::count_mismatch});
  CHECK(error_of(f.put<T>("v", four, {})).status ==
        NcStatus{WrapperFault::rank_mismatch});
  const Slab huge{
      {.start = 0, .count = std::numeric_limits<std::size_t>::max()},
      {.start = 0, .count = 2}};
  CHECK(error_of(f.put<T>("grid", four, huge)).status ==
        NcStatus{WrapperFault::overflow});
  CHECK(error_of(f.put<T>("nope", four, all4)).status ==
        NcStatus{LibraryStatus{nc_enotvar}});
  const std::vector<T> big((ReadLimits{}.max_att_bytes / sizeof(T)) + 1);
  CHECK(error_of(f.put_att<T>(global, "big", big)).status ==
        NcStatus{WrapperFault::too_large});
  CHECK(error_of(f.put_att<T>("nope", "a", four)).status ==
        NcStatus{LibraryStatus{nc_enotvar}});
  CHECK(error_of(f.define_var<T>("chunked", n,
                                 {.chunks = std::vector<std::size_t>{1, 1}}))
            .status == NcStatus{WrapperFault::rank_mismatch});
  // A name in use: netCDF-C refuses it.
  CHECK(not f.define_var<T>("v", grid, {}).has_value());
}

// dims n = 4, r = 2; variables of T unless noted:
//   v        fill -99, deflate 1, chunks {2}: {1, 2, -99, 4}, valid_min 0,
//            valid_max 50, scale_factor 2.0, add_offset 1.0
//   range    no fill attribute: {0, 2, 4, 3}, valid_range {0, 3}
//   badrange valid_range {0}      badmin valid_min of another type
//   badmax   valid_max of another type
//   badmissing missing_value of another type
//   badscale scale_factor int     badoffset add_offset int
//   unsigned _Unsigned "true"     other  of another type
//   grid(r, n)
// globals g = {1, 2} and empty = {} of T.
template <class T>
std::expected<void, Error> typed_file(File& f) {
  const DimInfo n = value_of(f.define_dim("n", 4));
  const DimInfo r = value_of(f.define_dim("r", 2));
  const std::array<DimInfo, 1> dims{n};
  const std::array<DimInfo, 2> grid{r, n};
  REQUIRE(f.define_var<T>("v", dims,
                          {.fill = fill_value<T>,
                           .deflate_level = 1,
                           .chunks = std::vector<std::size_t>{2}})
              .has_value());
  for (const NcNameRef name :
       {NcNameRef{"range"}, NcNameRef{"badrange"}, NcNameRef{"badmin"},
        NcNameRef{"badmax"}, NcNameRef{"badmissing"}, NcNameRef{"badscale"},
        NcNameRef{"badoffset"}, NcNameRef{"unsigned"}}) {
    REQUIRE(f.define_var<T>(name, dims, VarOptions<T>{}).has_value());
  }
  REQUIRE(f.define_var<Other<T>>("other", dims, {}).has_value());
  REQUIRE(f.define_var<T>("grid", grid, {}).has_value());
  const std::array<double, 1> two{2.0};
  const std::array<double, 1> one{1.0};
  const std::array<std::int32_t, 1> int_one{1};
  REQUIRE(att<T>(f, "v", "valid_min", {0}).has_value());
  REQUIRE(att<T>(f, "v", "valid_max", {50}).has_value());
  REQUIRE(f.put_att<double>("v", "scale_factor", two).has_value());
  REQUIRE(f.put_att<double>("v", "add_offset", one).has_value());
  REQUIRE(att<T>(f, "range", "valid_range", {0, 3}).has_value());
  REQUIRE(att<T>(f, "badrange", "valid_range", {0}).has_value());
  REQUIRE(att<Other<T>>(f, "badmin", "valid_min", {0}).has_value());
  REQUIRE(att<Other<T>>(f, "badmax", "valid_max", {0}).has_value());
  REQUIRE(att<Other<T>>(f, "badmissing", "missing_value", {0}).has_value());
  REQUIRE(
      f.put_att<std::int32_t>("badscale", "scale_factor", int_one).has_value());
  REQUIRE(
      f.put_att<std::int32_t>("badoffset", "add_offset", int_one).has_value());
  REQUIRE(f.put_att("unsigned", "_Unsigned", "true"sv).has_value());
  REQUIRE(att<T>(f, global, "g", {1, 2}).has_value());
  REQUIRE(att<T>(f, global, "empty", {}).has_value());
  check_write_errors<T>(f, dims, grid);
  REQUIRE(f.end_define().has_value());
  REQUIRE(put<T>(f, "v", {1, 2, -99, 4}).has_value());
  REQUIRE(put<T>(f, "range", {0, 2, 4, 3}).has_value());
  return {};
}

template <class T>
std::filesystem::path written(const ScratchDir& dir) {
  const auto path = dir / "typed.nc";
  REQUIRE(write_netcdf_atomic(path, typed_file<T>).has_value());
  return path;
}

}  // namespace

TEMPLATE_TEST_CASE("typed reads and attributes round-trip", "[io][netcdf]",
                   double, float, std::int8_t, std::int16_t, std::int32_t,
                   std::int64_t) {
  using T = TestType;
  const ScratchDir dir;
  const auto path = written<T>(dir);
  const File file = open(path);
  CHECK(value_of(file.read<T>("v", all4, ReadContext{})) ==
        values<T>({1, 2, -99, 4}));
  CHECK(status_of(error_of(file.read<T>("nope", all4, ReadContext{}))) ==
        NcStatus{LibraryStatus{nc_enotvar}});
  CHECK(file.numeric_att<T>(global, "g").value() == values<T>({1, 2}));
  CHECK(file.numeric_att<T>(global, "empty").value() == std::vector<T>{});
  CHECK(file.numeric_att<T>(global, "absent").value() == std::nullopt);
  CHECK(error_of(file.numeric_att<T>("badmin", "valid_min")).status ==
        NcStatus{WrapperFault::type_mismatch});
  CHECK(error_of(open(path, ReadLimits{.max_att_bytes = sizeof(T)})
                     .template numeric_att<T>(global, "g"))
            .status == NcStatus{WrapperFault::too_large});
  CHECK(error_of(file.numeric_att<T>("nope", "a")).status ==
        NcStatus{LibraryStatus{nc_enotvar}});
}

TEMPLATE_TEST_CASE("typed masking", "[io][netcdf]", double, float, std::int8_t,
                   std::int16_t, std::int32_t, std::int64_t) {
  using T = TestType;
  const ScratchDir dir;
  const File file = open(written<T>(dir));
  CHECK(file.masking<T>("v").value() == Masking<T>{.fill = fill_value<T>,
                                                   .valid_min = T{0},
                                                   .valid_max = T{50},
                                                   .scale = 2.0,
                                                   .offset = 1.0});
  const Masking<T> range = file.masking<T>("range").value();
  CHECK(range.valid_min == T{0});
  CHECK(range.valid_max == T{3});
  // Without a _FillValue attribute: the type's default fill, none for byte.
  CHECK(range.fill.has_value() == not std::same_as<T, std::int8_t>);
  const auto status = [&](NcNameRef var) {
    return error_of(file.masking<T>(var)).status;
  };
  CHECK(status("badrange") == NcStatus{WrapperFault::count_mismatch});
  CHECK(status("badmin") == NcStatus{WrapperFault::type_mismatch});
  CHECK(status("badmax") == NcStatus{WrapperFault::type_mismatch});
  CHECK(status("badmissing") == NcStatus{WrapperFault::type_mismatch});
  CHECK(status("badscale") == NcStatus{WrapperFault::type_mismatch});
  CHECK(status("badoffset") == NcStatus{WrapperFault::type_mismatch});
  CHECK(status("unsigned") == NcStatus{WrapperFault::unsupported_unsigned});
  CHECK(status("other") == NcStatus{WrapperFault::type_mismatch});
  CHECK(status("nope") == NcStatus{LibraryStatus{nc_enotvar}});

  const auto samples = file.read_samples("v", all4, ReadContext{});
  if constexpr (std::same_as<T, std::int64_t>) {
    CHECK(status_of(error_of(samples)) ==
          NcStatus{WrapperFault::type_mismatch});
  } else {
    // 1 + 2 * raw; -99 is the fill.
    CHECK(value_of(samples) ==
          std::vector<Sample>{mov::core::finite_or_missing(3),
                              mov::core::finite_or_missing(5), Missing{},
                              mov::core::finite_or_missing(9)});
    CHECK(status_of(
              error_of(file.read_samples("badrange", all4, ReadContext{}))) ==
          NcStatus{WrapperFault::count_mismatch});
  }
}
