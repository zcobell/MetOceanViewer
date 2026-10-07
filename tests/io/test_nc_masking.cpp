// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// nc::File::masking and read_samples: missing data in the variable's own
// type (B4, B9, B12).

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "mov/core/sample.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/netcdf/masking.hpp"
#include "nc_test_helpers.hpp"

namespace {

using mov::core::Missing;
using mov::core::Sample;
using mov::io::nc::File;
using mov::io::nc::Masking;
using mov::io::nc::NcNameRef;
using mov::io::nc::Slab;
using mov::test::nc::error_of;
using mov::test::nc::Fixtures;
using mov::test::nc::NcError;
using mov::test::nc::NcOp;
using mov::test::nc::NcStatus;
using mov::test::nc::open;
using mov::test::nc::ReadContext;
using mov::test::nc::status_of;
using mov::test::nc::value_of;
using mov::test::nc::WrapperFault;

const Slab all6{{.start = 0, .count = 6}};
const Sample missing{Missing{}};

Sample v(double x) { return mov::core::finite_or_missing(x); }

std::vector<Sample> samples(const File& file, NcNameRef var) {
  return value_of(file.read_samples(var, all6, ReadContext{}));
}

}  // namespace

TEST_CASE("_FillValue and NaN are masked in the native type (B4, B9)",
          "[io][netcdf][regression][B4][B9]") {
  Fixtures fx;
  const File file = open(fx.masking());
  const std::vector<Sample> filled{v(1), missing, v(2), missing, v(3), v(4)};
  CHECK(samples(file, "d_fill") == filled);
  CHECK(samples(file, "f_fill") == filled);
  CHECK(file.masking<float>("f_fill").value() ==
        Masking<float>{.fill = -99999.0F});
}

TEST_CASE("the library's default fill is masked (B9)",
          "[io][netcdf][regression][B9]") {
  Fixtures fx;
  const File file = open(fx.masking());
  // v4 compared nothing: 9.969e36 reached the plot.
  const std::vector<Sample> head{v(1), v(2), v(3), missing, missing, missing};
  CHECK(samples(file, "d_default") == head);
  CHECK(samples(file, "f_default") == head);
  CHECK(file.masking<double>("d_default").value().fill ==
        9.9692099683868690e+36);
  // Bytes have no default fill (they are often flags): -127 is a value.
  CHECK(samples(file, "b_default") ==
        std::vector<Sample>{v(1), v(2), v(3), v(-127), v(-127), v(-127)});
  // NC_NOFILL: the default fill value is data.
  CHECK(samples(file, "d_nofill") ==
        std::vector<Sample>{v(1), v(2), v(3), v(9.9692099683868690e+36), v(5),
                            v(6)});
  CHECK(file.masking<double>("d_nofill").value().fill == std::nullopt);
}

TEST_CASE("the fill comes from the variable, not a hard-coded -999 (B12)",
          "[io][netcdf][regression][B12]") {
  Fixtures fx;
  const File file = open(fx.masking());
  // v4's D-Flow reader compared every value with -999.0.
  CHECK(samples(file, "d_fill999") ==
        std::vector<Sample>{missing, v(-999.0000001), v(-5), v(1), v(2), v(3)});
  CHECK(samples(file, "f_fill5") ==
        std::vector<Sample>{missing, v(-999), v(1), v(2), v(3), v(4)});
}

TEST_CASE("missing_value, valid range and unpacking", "[io][netcdf]") {
  Fixtures fx;
  const File file = open(fx.masking());
  CHECK(samples(file, "f_missing") ==
        std::vector<Sample>{v(1), missing, missing, v(2), v(3), v(4)});
  CHECK(samples(file, "d_missing1") ==
        std::vector<Sample>{missing, v(0), v(1), v(2), v(3), v(4)});
  const std::vector<Sample> ranged{missing, v(0), v(5), v(10), missing, v(3)};
  CHECK(samples(file, "d_valid") == ranged);
  CHECK(samples(file, "d_range") == ranged);
  // Unpacked after the fill test: 10 + 0.5 * raw.
  CHECK(samples(file, "s_packed") ==
        std::vector<Sample>{v(10), v(11), missing, v(12), v(13), v(14)});
  CHECK(file.masking<std::int16_t>("s_packed").value() ==
        Masking<std::int16_t>{
            .fill = std::int16_t{-32767}, .scale = 0.5, .offset = 10.0});
}

TEST_CASE("an unpacked value that overflows is missing", "[io][netcdf]") {
  const Masking<double> huge{.scale = 1e300};
  CHECK(huge.apply(1e300) == missing);
  CHECK(huge.apply(1.0) == v(1e300));
}

TEST_CASE("masking refuses what it cannot apply exactly", "[io][netcdf]") {
  Fixtures fx;
  const auto path = fx.masking();
  const File file = open(path);
  CHECK(error_of(file.masking<std::int8_t>("b_unsigned")) ==
        NcError{.status = WrapperFault::unsupported_unsigned,
                .op = NcOp::get_att,
                .object = "b_unsigned",
                .file = path});
  CHECK(status_of(
            error_of(file.read_samples("b_unsigned", all6, ReadContext{}))) ==
        NcStatus{WrapperFault::unsupported_unsigned});
  // 64-bit integers do not fit a double exactly.
  CHECK(
      status_of(error_of(file.read_samples("i_int64", all6, ReadContext{}))) ==
      NcStatus{WrapperFault::type_mismatch});
  CHECK(error_of(file.masking<float>("d_fill")).status ==
        NcStatus{WrapperFault::type_mismatch});
  CHECK(error_of(file.masking<double>("d_badscale")) ==
        NcError{.status = WrapperFault::type_mismatch,
                .op = NcOp::get_att,
                .object = "d_badscale:scale_factor",
                .file = path});
  CHECK(error_of(file.masking<double>("d_badrange")) ==
        NcError{.status = WrapperFault::count_mismatch,
                .op = NcOp::get_att,
                .object = "d_badrange:valid_range",
                .file = path});
  CHECK(error_of(file.masking<double>("d_badmissing")).status ==
        NcStatus{WrapperFault::type_mismatch});
}

TEST_CASE("read_samples of text and string variables", "[io][netcdf]") {
  Fixtures fx;
  const File file = open(fx.typed());
  const Slab all4{{.start = 0, .count = 4}};
  CHECK(
      status_of(error_of(file.read_samples("v_string", all4, ReadContext{}))) ==
      NcStatus{WrapperFault::type_mismatch});
  CHECK(
      status_of(error_of(file.read_samples("v_ubyte", all4, ReadContext{}))) ==
      NcStatus{WrapperFault::type_mismatch});
  CHECK(value_of(file.read_samples("v_float", all4, ReadContext{})) ==
        std::vector<Sample>{v(1.5), v(-2.25), v(3), v(4)});
  CHECK(value_of(file.read_samples("v_byte", all4, ReadContext{})) ==
        std::vector<Sample>{v(1), v(-2), v(3), v(4)});
  CHECK(value_of(file.read_samples("v_short", all4, ReadContext{})) ==
        std::vector<Sample>{v(1), v(-2), v(300), v(4)});
  CHECK(value_of(file.read_samples("v_int", all4, ReadContext{})) ==
        std::vector<Sample>{v(1), v(-2), v(70000), v(4)});
}
