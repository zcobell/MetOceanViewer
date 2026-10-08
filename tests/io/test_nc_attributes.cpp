// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// nc::File attribute reads.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "nc_test_helpers.hpp"

namespace {

using mov::io::nc::File;
using mov::io::nc::global;
using mov::test::nc::error_of;
using mov::test::nc::Fixtures;
using mov::test::nc::LibraryStatus;
using mov::test::nc::NcError;
using mov::test::nc::NcOp;
using mov::test::nc::NcStatus;
using mov::test::nc::open;
using mov::test::nc::ReadLimits;
using mov::test::nc::WrapperFault;

std::string alphabet(std::size_t n) {
  std::string s(n, 'a');
  for (std::size_t i = 0; i < n; ++i) {
    s[i] = static_cast<char>('a' + static_cast<int>(i % 26));
  }
  return s;
}

}  // namespace

TEST_CASE("text_att is sized from the attribute's length (B8)",
          "[io][netcdf][regression][B8]") {
  Fixtures fx;
  // v4 read every text attribute into an 80-byte buffer.
  const File file = open(fx.attributes(120));
  CHECK(file.text_att(global, "history").value() == alphabet(120));
  CHECK(file.text_att(global, "title").value() == "Gen\xc3\xa8ve \xe2\x9c\x93");
  CHECK(file.text_att(global, "empty").value() == "");
  CHECK(file.text_att("x", "units").value() == "degrees_east");
  CHECK(file.text_att(global, "absent").value() == std::nullopt);
  CHECK(file.text_att("x", "absent").value() == std::nullopt);
}

TEST_CASE("text_att limits and types", "[io][netcdf]") {
  Fixtures fx;
  const auto path = fx.attributes(120);
  CHECK(open(path, ReadLimits{.max_att_bytes = 120})
            .text_att(global, "history")
            .value()
            .value_or(std::string{})
            .size() == 120);
  CHECK(error_of(open(path, ReadLimits{.max_att_bytes = 119})
                     .text_att(global, "history")) ==
        NcError{.status = WrapperFault::too_large,
                .op = NcOp::get_att,
                .object = ":history",
                .file = path});
  CHECK(open(path, ReadLimits{.max_att_bytes = 4})
            .text_att(global, "s_title")
            .error()
            .status == NcStatus{WrapperFault::too_large});
  // One handle per file: the limited opens above are gone by now.
  const File file = open(path);
  // NC_STRING: one string reads as text; two do not.
  CHECK(file.text_att(global, "s_title").value() == "hello");
  CHECK(error_of(file.text_att(global, "s_pair")).status ==
        NcStatus{WrapperFault::count_mismatch});
  CHECK(error_of(file.text_att(global, "doubles")).status ==
        NcStatus{WrapperFault::type_mismatch});
  // An attribute of a variable that does not exist.
  CHECK(error_of(file.text_att("nope", "units")) ==
        NcError{.status = LibraryStatus{-49},  // NC_ENOTVAR
                .op = NcOp::get_att,
                .object = "nope:units",
                .file = path});
}

TEST_CASE("numeric_att requires the exact type (B10)",
          "[io][netcdf][regression][B10]") {
  Fixtures fx;
  const auto path = fx.attributes();
  CHECK(open(path, ReadLimits{.max_att_bytes = 23})
            .numeric_att<double>(global, "doubles")
            .error()
            .status == NcStatus{WrapperFault::too_large});
  CHECK(open(path, ReadLimits{.max_att_bytes = 24})
            .numeric_att<double>(global, "doubles")
            .has_value());
  // One handle per file: the limited opens above are gone by now.
  const File file = open(path);
  CHECK(
      file.numeric_att<std::int32_t>("x", "HorizontalProjectionEPSG").value() ==
      std::vector<std::int32_t>{4326});
  // v4's getEpsg returned the netCDF error code of this read as the EPSG.
  CHECK(error_of(
            file.numeric_att<std::int32_t>("y", "HorizontalProjectionEPSG")) ==
        NcError{.status = WrapperFault::type_mismatch,
                .op = NcOp::get_att,
                .object = "y:HorizontalProjectionEPSG",
                .file = path});
  CHECK(file.numeric_att<std::int32_t>("y", "absent").value() == std::nullopt);
  CHECK(file.numeric_att<double>(global, "doubles").value() ==
        std::vector<double>{1.5, 2.5, 3.5});
  CHECK(error_of(file.numeric_att<float>(global, "doubles")).status ==
        NcStatus{WrapperFault::type_mismatch});
  CHECK(error_of(file.numeric_att<std::int8_t>(global, "flags")).status ==
        NcStatus{WrapperFault::type_mismatch});
  CHECK(error_of(file.numeric_att<double>(global, "title")).status ==
        NcStatus{WrapperFault::type_mismatch});
}
