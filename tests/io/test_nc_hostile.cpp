// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The hostile-structure set (core-design.md 7.3): each file gives a specific
// error or a defined value, never a crash or an allocation sized by the file.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mov/core/sample.hpp"
#include "mov/io/netcdf/file.hpp"
#include "nc_test_helpers.hpp"

namespace {

using mov::core::Missing;
using mov::core::Sample;
using mov::io::nc::File;
using mov::io::nc::global;
using mov::io::nc::Slab;
using mov::test::nc::error_of;
using mov::test::nc::Fixtures;
using mov::test::nc::must;
using mov::test::nc::NcError;
using mov::test::nc::NcOp;
using mov::test::nc::NcStatus;
using mov::test::nc::open;
using mov::test::nc::ReadLimits;
using mov::test::nc::status_of;
using mov::test::nc::value_of;
using mov::test::nc::WrapperFault;
using mov::test::ncgen::Hostile;

constexpr std::size_t two_to_40 = std::size_t{1} << 40U;

}  // namespace

TEST_CASE("hostile: a 2^40 dimension is too large to read", "[io][netcdf]") {
  Fixtures fx;
  const File file = open(fx.hostile(Hostile::huge_dim));
  CHECK(must(file.find_dim("big").value()).length == two_to_40);
  const Slab all{{.start = 0, .count = two_to_40}};
  CHECK(status_of(error_of(file.read<double>("empty", all))) ==
        NcStatus{WrapperFault::too_large});
  CHECK(status_of(error_of(file.read_samples("empty", all))) ==
        NcStatus{WrapperFault::too_large});
  // 2^110 elements do not even fit in size_t.
  const Slab cube{{.start = 0, .count = two_to_40},
                  {.start = 0, .count = two_to_40},
                  {.start = 0, .count = std::size_t{1} << 30U}};
  CHECK(status_of(error_of(file.read<double>("square", cube))) ==
        NcStatus{WrapperFault::overflow});
  // A part of it is fine: unwritten chunks read as the fill value.
  const Slab head{{.start = two_to_40 - 2, .count = 2}};
  CHECK(value_of(file.read_samples("empty", head)) ==
        std::vector<Sample>{Missing{}, Missing{}});
}

TEST_CASE("hostile: an attribute over 1 MiB", "[io][netcdf]") {
  Fixtures fx;
  const auto path = fx.hostile(Hostile::big_attribute);
  CHECK(error_of(open(path).text_att(global, "history")) ==
        NcError{.status = WrapperFault::too_large,
                .op = NcOp::get_att,
                .object = ":history",
                .file = path});
  CHECK(open(path, ReadLimits{.max_att_bytes = (std::size_t{1} << 20U) + 1})
            .text_att(global, "history")
            .value()
            .value_or(std::string{})
            .size() == (std::size_t{1} << 20U) + 1);
}

TEST_CASE("hostile: a NULL NC_STRING element reads as empty", "[io][netcdf]") {
  Fixtures fx;
  const File file = open(fx.hostile(Hostile::string_null));
  CHECK(value_of(file.read_strings("ids")) ==
        std::vector<std::string>{"a", "", "ccc"});
}

TEST_CASE("hostile: a _FillValue of the wrong type or length", "[io][netcdf]") {
  Fixtures fx;
  // netCDF-C's nc_inq_var_fill would copy the 4-byte float fill into an
  // 8-byte double, or the first of two values; masking refuses both.
  const auto wrong_type = fx.hostile(Hostile::fill_wrong_type);
  const File typed = open(wrong_type);
  CHECK(error_of(typed.masking<double>("v")) ==
        NcError{.status = WrapperFault::type_mismatch,
                .op = NcOp::get_att,
                .object = "v:_FillValue",
                .file = wrong_type});
  const Slab two{{.start = 0, .count = 2}};
  CHECK(status_of(error_of(typed.read_samples("v", two))) ==
        NcStatus{WrapperFault::type_mismatch});
  // The data themselves still read.
  CHECK(value_of(typed.read<double>("v", two)) == std::vector<double>{1, -1});
  const File pair = open(fx.hostile(Hostile::fill_two_values));
  CHECK(error_of(pair.masking<double>("v")).status ==
        NcStatus{WrapperFault::count_mismatch});
}

TEST_CASE("hostile: an unlimited name length of 0", "[io][netcdf]") {
  Fixtures fx;
  const auto path = fx.hostile(Hostile::zero_length_name_len);
  {
    const File file = open(path);
    CHECK(must(file.find_dim("name_len").value()).length == 0);
    CHECK(value_of(file.read_char_rows("station_name")) ==
          std::vector<std::string>{"", ""});
  }  // one handle per file
  CHECK(status_of(error_of(
            open(path, {.max_elements = 1}).read_char_rows("station_name"))) ==
        NcStatus{WrapperFault::too_large});
}

TEST_CASE("hostile: a time variable that is all fill", "[io][netcdf]") {
  Fixtures fx;
  const File file = open(fx.hostile(Hostile::fill_valued_time));
  CHECK(value_of(file.read_samples("time", {{.start = 0, .count = 3}})) ==
        std::vector<Sample>(3, Sample{Missing{}}));
}

TEST_CASE("hostile: an EPSG code stored as text (B10)",
          "[io][netcdf][regression][B10]") {
  Fixtures fx;
  const File file = open(fx.hostile(Hostile::epsg_as_text));
  CHECK(
      error_of(file.numeric_att<std::int32_t>("x", "HorizontalProjectionEPSG"))
          .status == NcStatus{WrapperFault::type_mismatch});
  CHECK(file.text_att("x", "HorizontalProjectionEPSG").value() == "4326");
}
