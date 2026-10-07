// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// nc::File data reads: the type table, slabs, limits, cancellation, char
// rows and strings.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
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
using mov::io::StopToken;
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
using mov::test::nc::ReadContext;
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
    const auto read = file.read<T>(name, all4, ReadContext{});
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
            "v_char", {{.start = 0, .count = 4}, {.start = 0, .count = 3}},
            ReadContext{}))) == NcStatus{WrapperFault::type_mismatch});
}

TEST_CASE("read of a scalar, a hyperslab and a 3-D variable", "[io][netcdf]") {
  Fixtures fx;
  const File file = open(fx.typed());
  CHECK(value_of(file.read<double>("s_double", {}, ReadContext{})) ==
        std::vector<double>{7.5});
  const Slab corner{{.start = 1, .count = 2}, {.start = 1, .count = 3}};
  CHECK(value_of(file.read<double>("grid", corner, ReadContext{})) ==
        std::vector<double>{11, 12, 13, 21, 22, 23});
  const Slab whole{{.start = 0, .count = 2},
                   {.start = 0, .count = 3},
                   {.start = 0, .count = 4}};
  const std::vector<std::int32_t> expected =
      value_of(file.read<std::int32_t>("cube", whole, ReadContext{}));
  REQUIRE(expected.size() == 24);
  CHECK(expected[23] == 123);
  // The same values whatever the block size.
  for (const std::size_t block : {1UZ, 2UZ, 3UZ, 5UZ, 7UZ, 12UZ, 13UZ}) {
    CAPTURE(block);
    const ReadContext ctx{.limits = {.slab_elements = block}};
    CHECK(value_of(file.read<std::int32_t>("cube", whole, ctx)) == expected);
  }
  const Slab middle{{.start = 1, .count = 1},
                    {.start = 1, .count = 2},
                    {.start = 2, .count = 2}};
  CHECK(value_of(file.read<std::int64_t>(
            "cube", middle, ReadContext{.limits = {.slab_elements = 1}})) ==
        std::vector<std::int64_t>{112, 113, 122, 123});
}

TEST_CASE("read checks the slab before allocating", "[io][netcdf]") {
  Fixtures fx;
  const File file = open(fx.typed());
  const auto status = [&](const Slab& slab, const ReadLimits& limits) {
    return status_of(error_of(
        file.read<double>("grid", slab, ReadContext{.limits = limits})));
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
  CHECK(value_of(file.read<double>("grid", whole,
                                   ReadContext{.limits = {.max_elements = 12}}))
            .size() == 12);
  // An empty hyperslab reads nothing.
  CHECK(
      value_of(file.read<double>(
                   "grid", {{.start = 3, .count = 0}, {.start = 0, .count = 4}},
                   ReadContext{}))
          .empty());
  // A missing variable.
  CHECK(status_of(error_of(file.read<double>("nope", all4, ReadContext{}))) ==
        NcStatus{LibraryStatus{-49}});  // NC_ENOTVAR
}

TEST_CASE("read polls the stop request between blocks", "[io][netcdf]") {
  const Fixtures fx;
  mov::test::ncgen::make_matrix(fx.path("m.nc"), 100, 10);
  const File file = open(fx.path("m.nc"));
  const Slab whole{{.start = 0, .count = 100}, {.start = 0, .count = 10}};
  int polls = 0;
  const ReadContext stop_after_3{
      .limits = {.slab_elements = 50},
      .stop = StopToken{[&] { return ++polls > 3; }}};
  const auto read = file.read<double>("data", whole, stop_after_3);
  REQUIRE(not read.has_value());
  CHECK(std::holds_alternative<Cancelled>(read.error()));
  CHECK(polls == 4);
  const ReadContext stopped{.stop = StopToken{[] { return true; }}};
  CHECK(std::holds_alternative<Cancelled>(
      error_of(file.read<double>("data", whole, stopped))));
  CHECK(std::holds_alternative<Cancelled>(
      error_of(file.read_samples("data", whole, stopped))));
  const auto full = value_of(file.read<double>(
      "data", whole, ReadContext{.limits = {.slab_elements = 7}}));
  CHECK(full.front() == 0);
  CHECK(full.back() == 999);
}

TEST_CASE("read_char_rows returns raw rows of the file's stride",
          "[io][netcdf]") {
  Fixtures fx;
  const File file = open(fx.typed());
  CHECK(value_of(file.read_char_rows("v_char", ReadContext{})) ==
        std::vector<std::string>{"abc", "de\0"s, "fgh", "\0\0\0"s});
  CHECK(value_of(file.read_char_rows(
            "v_char", ReadContext{.limits = {.slab_elements = 1}})) ==
        std::vector<std::string>{"abc", "de\0"s, "fgh", "\0\0\0"s});
  CHECK(status_of(error_of(file.read_char_rows("v_double", ReadContext{}))) ==
        NcStatus{WrapperFault::type_mismatch});
  CHECK(status_of(error_of(file.read_char_rows(
            "v_char", ReadContext{.limits = {.max_elements = 11}}))) ==
        NcStatus{WrapperFault::too_large});
}

TEST_CASE("read_strings reads NC_STRING and frees it (B21)", "[io][netcdf]") {
  Fixtures fx;
  const File file = open(fx.typed());
  const std::vector<std::string> expected{"one", "two", "three", "four"};
  CHECK(value_of(file.read_strings("v_string", ReadContext{})) == expected);
  CHECK(value_of(file.read_strings(
            "v_string", ReadContext{.limits = {.slab_elements = 3}})) ==
        expected);
  CHECK(status_of(error_of(file.read_strings("v_char", ReadContext{}))) ==
        NcStatus{WrapperFault::type_mismatch});
  // 15 bytes in all.
  CHECK(status_of(error_of(file.read_strings(
            "v_string", ReadContext{.limits = {.max_text_bytes = 14}}))) ==
        NcStatus{WrapperFault::too_large});
  CHECK(value_of(file.read_strings(
                     "v_string", ReadContext{.limits = {.max_text_bytes = 15}}))
            .size() == 4);
  CHECK(status_of(error_of(file.read_strings(
            "v_string", ReadContext{.limits = {.max_elements = 3}}))) ==
        NcStatus{WrapperFault::too_large});
}
