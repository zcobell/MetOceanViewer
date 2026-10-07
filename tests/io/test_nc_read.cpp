// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// nc::File data reads: the type table, slabs, limits, cancellation, char
// rows and strings.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "nc_test_helpers.hpp"

namespace {

using namespace std::string_literals;
using mov::io::Cancelled;
using mov::io::Error;
using mov::io::StopToken;
using mov::io::nc::DimRange;
using mov::io::nc::File;
using mov::io::nc::NcName;
using mov::io::nc::readable_as;
using mov::io::nc::Slab;
using mov::io::nc::Type;
using mov::test::nc::error_of;
using mov::test::nc::Fixtures;
using mov::test::nc::LibraryStatus;
using mov::test::nc::NcStatus;
using mov::test::nc::open;
using mov::test::nc::ReadLimits;
using mov::test::nc::status_of;
using mov::test::nc::value_of;
using mov::test::nc::WrapperFault;

constexpr int nc_einvalcoords = -40;  // NC_EINVALCOORDS
constexpr int nc_eedge = -57;         // NC_EEDGE

const Slab all4{{.start = 0, .count = 4}};

struct Case {
  const char* var;
  Type type;
  std::vector<double> values;
};

// make_typed's numeric variables and their values.
const std::vector<Case> typed_cases{
    {.var = "v_double", .type = Type::double_, .values = {1.5, -2.25, 3, 4}},
    {.var = "v_float", .type = Type::float_, .values = {1.5, -2.25, 3, 4}},
    {.var = "v_byte", .type = Type::byte, .values = {1, -2, 3, 4}},
    {.var = "v_short", .type = Type::short_, .values = {1, -2, 300, 4}},
    {.var = "v_int", .type = Type::int_, .values = {1, -2, 70000, 4}},
    {.var = "v_int64",
     .type = Type::int64,
     .values = {1, -2, 1099511627776.0, 4}},
    {.var = "v_ubyte", .type = Type::ubyte, .values = {1, 2, 3, 4}},
    {.var = "v_ushort", .type = Type::ushort, .values = {1, 2, 3, 4}},
    {.var = "v_uint", .type = Type::uint, .values = {1, 2, 3, 4}},
    {.var = "v_uint64", .type = Type::uint64, .values = {1, 2, 3, 4}},
    {.var = "v_string", .type = Type::string, .values = {}},
    {.var = "v_opaque", .type = Type::other, .values = {}},
};

template <class T>
void check_read_table(const File& file) {
  for (const Case& c : typed_cases) {
    CAPTURE(c.var, sizeof(T));
    const NcName name = NcName::make(c.var).value();
    const auto read = file.read<T>(name, all4);
    if (readable_as<T>(c.type)) {
      REQUIRE(read.has_value());
      std::vector<double> widened;
      for (const T v : *read) {
        widened.push_back(static_cast<double>(v));
      }
      CHECK(widened == c.values);
    } else {
      CHECK(status_of(error_of(read)) == NcStatus{WrapperFault::type_mismatch});
    }
  }
}

}  // namespace

TEST_CASE("read<T> converts only where no value changes (section 4.3, B4)",
          "[io][netcdf][regression][B4]") {
  Fixtures fx;
  const File file = open(fx.typed());
  check_read_table<double>(file);
  check_read_table<float>(file);
  check_read_table<std::int8_t>(file);
  check_read_table<std::int16_t>(file);
  check_read_table<std::int32_t>(file);
  check_read_table<std::int64_t>(file);
  // The text variable is no number.
  CHECK(status_of(error_of(file.read<double>(
            "v_char", {{.start = 0, .count = 4}, {.start = 0, .count = 3}}))) ==
        NcStatus{WrapperFault::type_mismatch});
}

TEST_CASE("read of a scalar, a hyperslab and a 3-D variable", "[io][netcdf]") {
  Fixtures fx;
  const File file = open(fx.typed());
  CHECK(value_of(file.read<double>("s_double", {})) ==
        std::vector<double>{7.5});
  const Slab corner{{.start = 1, .count = 2}, {.start = 1, .count = 3}};
  CHECK(value_of(file.read<double>("grid", corner)) ==
        std::vector<double>{11, 12, 13, 21, 22, 23});
  const Slab whole{{.start = 0, .count = 2},
                   {.start = 0, .count = 3},
                   {.start = 0, .count = 4}};
  const std::vector<std::int32_t> expected =
      value_of(file.read<std::int32_t>("cube", whole));
  REQUIRE(expected.size() == 24);
  CHECK(expected[23] == 123);
  // The same values whatever the block size.
  for (const std::size_t block : {1UZ, 2UZ, 3UZ, 5UZ, 7UZ, 12UZ, 13UZ}) {
    CAPTURE(block);
    const File small = open(fx.typed(), {.slab_elements = block});
    CHECK(value_of(small.read<std::int32_t>("cube", whole)) == expected);
  }
  const Slab middle{{.start = 1, .count = 1},
                    {.start = 1, .count = 2},
                    {.start = 2, .count = 2}};
  CHECK(value_of(open(fx.typed(), {.slab_elements = 1})
                     .read<std::int64_t>("cube", middle)) ==
        std::vector<std::int64_t>{112, 113, 122, 123});
}

TEST_CASE("read checks the slab before allocating", "[io][netcdf]") {
  Fixtures fx;
  const auto status = [&](const Slab& slab, const ReadLimits& limits) {
    return status_of(
        error_of(open(fx.typed(), limits).read<double>("grid", slab)));
  };
  CHECK(status({{.start = 0, .count = 3}}, {}) ==
        NcStatus{WrapperFault::rank_mismatch});
  CHECK(status({{.start = 4, .count = 0}, {.start = 0, .count = 4}}, {}) ==
        NcStatus{LibraryStatus{nc_einvalcoords}});
  CHECK(status({{.start = 2, .count = 2}, {.start = 0, .count = 4}}, {}) ==
        NcStatus{LibraryStatus{nc_eedge}});
  const Slab whole{{.start = 0, .count = 3}, {.start = 0, .count = 4}};
  CHECK(status(whole, {.max_elements = 11}) ==
        NcStatus{WrapperFault::too_large});
  // 12 doubles are 96 bytes.
  CHECK(status(whole, {.max_result_bytes = 95}) ==
        NcStatus{WrapperFault::too_large});
  CHECK(value_of(open(fx.typed(), {.max_elements = 12, .max_result_bytes = 96})
                     .read<double>("grid", whole))
            .size() == 12);
  // read_samples charges 16 bytes per Sample.
  CHECK(status_of(error_of(open(fx.typed(), {.max_result_bytes = 191})
                               .read_samples("grid", whole))) ==
        NcStatus{WrapperFault::too_large});
  CHECK(open(fx.typed(), {.max_result_bytes = 192})
            .read_samples("grid", whole)
            .has_value());
  const File file = open(fx.typed());
  // An empty hyperslab reads nothing.
  CHECK(value_of(file.read<double>("grid", {{.start = 3, .count = 0},
                                            {.start = 0, .count = 4}}))
            .empty());
  // A missing variable.
  CHECK(status_of(error_of(file.read<double>("nope", all4))) ==
        NcStatus{LibraryStatus{-49}});  // NC_ENOTVAR
}

TEST_CASE("read polls the stop request between blocks", "[io][netcdf]") {
  const Fixtures fx;
  mov::test::ncgen::make_matrix(fx.path("m.nc"), 100, 10);
  const File file = open(fx.path("m.nc"), {.slab_elements = 50});
  const Slab whole{{.start = 0, .count = 100}, {.start = 0, .count = 10}};
  int polls = 0;
  const StopToken stop_after_3{[&] { return ++polls > 3; }};
  const auto read = file.read<double>("data", whole, stop_after_3);
  REQUIRE(not read.has_value());
  CHECK(std::holds_alternative<Cancelled>(read.error()));
  CHECK(polls == 4);
  const StopToken stopped{[] { return true; }};
  CHECK(std::holds_alternative<Cancelled>(
      error_of(file.read<double>("data", whole, stopped))));
  CHECK(std::holds_alternative<Cancelled>(
      error_of(file.read_samples("data", whole, stopped))));
  CHECK(std::holds_alternative<Cancelled>(error_of(file.read<double>(
      "data", {{.start = 0, .count = 1}, {.start = 0, .count = 1}}, stopped))));
  const auto full = value_of(
      open(fx.path("m.nc"), {.slab_elements = 7}).read<double>("data", whole));
  CHECK(full.front() == 0);
  CHECK(full.back() == 999);
}

TEST_CASE("read_blocks hands over whole rows of the outer dimension",
          "[io][netcdf]") {
  Fixtures fx;
  mov::test::ncgen::make_matrix(fx.path("m.nc"), 10, 4);
  // Blocks of 12 elements hold 6 rows of the slab's 2 columns: rows 2..7,
  // then row 8.
  const File file = open(fx.path("m.nc"), {.slab_elements = 12});
  const Slab slab{{.start = 2, .count = 7}, {.start = 1, .count = 2}};
  std::vector<DimRange> outers;
  std::vector<double> seen;
  const auto done = file.read_blocks<double>(
      "data", slab,
      [&](std::span<const double> values,
          DimRange outer) -> std::expected<void, Error> {
        outers.push_back(outer);
        seen.insert(seen.end(), values.begin(), values.end());
        return {};
      });
  REQUIRE(done.has_value());
  // rows_per_block counts the slab's inner range (2), not the dimension's.
  CHECK(mov::io::nc::rows_per_block(slab, 12) == 6);
  CHECK(outers == std::vector<DimRange>{{.start = 2, .count = 6},
                                        {.start = 8, .count = 1}});
  CHECK(seen == value_of(file.read<double>("data", slab)));
  // The visitor's error ends the read.
  int calls = 0;
  const auto stopped = file.read_blocks<double>(
      "data", slab,
      [&](std::span<const double>, DimRange) -> std::expected<void, Error> {
        ++calls;
        return std::unexpected{Error{Cancelled{}}};
      });
  CHECK(std::holds_alternative<Cancelled>(error_of(stopped)));
  CHECK(calls == 1);
  CHECK(status_of(error_of(file.read_blocks<float>(
            "data", slab, [](std::span<const float>, DimRange) {
              return std::expected<void, Error>{};
            }))) == NcStatus{WrapperFault::type_mismatch});
  // A scalar is one block of outer {0, 1}.
  const File typed = open(fx.typed());
  std::vector<DimRange> scalar;
  REQUIRE(typed
              .read_blocks<double>("s_double", {},
                                   [&](std::span<const double> v, DimRange o)
                                       -> std::expected<void, Error> {
                                     CHECK(v.size() == 1);
                                     scalar.push_back(o);
                                     return {};
                                   })
              .has_value());
  CHECK(scalar == std::vector<DimRange>{{.start = 0, .count = 1}});
}

TEST_CASE("read_char_rows returns raw rows of the file's stride",
          "[io][netcdf]") {
  Fixtures fx;
  const File file = open(fx.typed());
  const std::vector<std::string> rows{"abc", "de\0"s, "fgh", "\0\0\0"s};
  CHECK(value_of(file.read_char_rows("v_char")) == rows);
  CHECK(value_of(
            open(fx.typed(), {.slab_elements = 1}).read_char_rows("v_char")) ==
        rows);
  CHECK(status_of(error_of(file.read_char_rows("v_double"))) ==
        NcStatus{WrapperFault::type_mismatch});
  CHECK(status_of(error_of(file.read_char_rows("s_double"))) ==
        NcStatus{WrapperFault::type_mismatch});
  CHECK(status_of(error_of(
            open(fx.typed(), {.max_elements = 11}).read_char_rows("v_char"))) ==
        NcStatus{WrapperFault::too_large});
  // 4 rows of 32 bytes, plus 12 bytes read and 12 copied.
  constexpr std::size_t needed = (4 * sizeof(std::string)) + 24;
  CHECK(status_of(error_of(open(fx.typed(), {.max_result_bytes = needed - 1})
                               .read_char_rows("v_char"))) ==
        NcStatus{WrapperFault::too_large});
  CHECK(open(fx.typed(), {.max_result_bytes = needed})
            .read_char_rows("v_char")
            .has_value());
}

TEST_CASE("read_char_rows of a 1-D and a 3-D char variable", "[io][netcdf]") {
  const Fixtures fx;
  mov::test::ncgen::make_char_shapes(fx.path("chars.nc"));
  const File file = open(fx.path("chars.nc"));
  CHECK(value_of(file.read_char_rows("one")) ==
        std::vector<std::string>{"hello"});
  CHECK(value_of(file.read_char_rows("three")) ==
        std::vector<std::string>{"ab", "cd", "ef", "gh", "ij", "kl"});
  CHECK(status_of(error_of(file.read_char_rows("scalar"))) ==
        NcStatus{WrapperFault::rank_mismatch});
}

TEST_CASE("read_strings reads NC_STRING and frees it (B21)", "[io][netcdf]") {
  // One handle at a time: netCDF-C 4.9.3 with HDF5 2.1.1 crashes when a file
  // whose NC_STRING data was read through a second handle is opened again
  // (docs/wp-notes/WP6.md, "Library defect").
  Fixtures fx;
  const std::vector<std::string> expected{"one", "two", "three", "four"};
  {
    const File file = open(fx.typed());
    CHECK(value_of(file.read_strings("v_string")) == expected);
    CHECK(status_of(error_of(file.read_strings("v_char"))) ==
          NcStatus{WrapperFault::type_mismatch});
  }
  CHECK(value_of(
            open(fx.typed(), {.slab_elements = 3}).read_strings("v_string")) ==
        expected);
  // 4 strings of 32 bytes plus 15 bytes of text.
  constexpr std::size_t needed = (4 * sizeof(std::string)) + 15;
  CHECK(status_of(error_of(open(fx.typed(), {.max_result_bytes = needed - 1})
                               .read_strings("v_string"))) ==
        NcStatus{WrapperFault::too_large});
  CHECK(value_of(open(fx.typed(), {.max_result_bytes = needed})
                     .read_strings("v_string"))
            .size() == 4);
  CHECK(status_of(error_of(open(fx.typed(), {.max_result_bytes = 64})
                               .read_strings("v_string"))) ==
        NcStatus{WrapperFault::too_large});
  CHECK(status_of(error_of(
            open(fx.typed(), {.max_elements = 3}).read_strings("v_string"))) ==
        NcStatus{WrapperFault::too_large});
}
