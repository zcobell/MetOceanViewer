// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Regression tests of plan section 1.2 bugs 5, 7 and 11 (the others are
// tagged where their component is tested: B4 test_nc_read/_masking/_write,
// B6 test_nc_file/_write, B8 and B10 test_nc_attributes, B9 test_nc_masking,
// B12 test_nc_file/_masking, B15 test_nc_write).

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <string>
#include <vector>

#include "mov/io/netcdf/file.hpp"
#include "nc_counts.hpp"
#include "nc_test_helpers.hpp"

namespace {

using namespace std::string_literals;
using mov::io::nc::File;
using mov::io::nc::global;
using mov::io::nc::Slab;
using mov::test::nc::Fixtures;
using mov::test::nc::open;
using mov::test::nc::ReadLimits;
using mov::test::nc::value_of;
namespace counts = mov::test::nc_counts;
namespace ncgen = mov::test::ncgen;

// `n` bytes of a name: the text, then NULs, then junk after the first NUL
// when there is room (v4's over-read could leave it there).
std::string padded(const std::string& text, std::size_t n) {
  std::string row = text.substr(0, n);
  if (row.size() < n) {
    row += '\0';
  }
  while (row.size() < n) {
    row += 'J';
  }
  return row;
}

}  // namespace

TEST_CASE("every open is closed on every error path (B5)",
          "[io][netcdf][regression][B5][linux]") {
  if constexpr (not counts::available) {
    SKIP("needs the --wrap shims (Linux)");
  }
  Fixtures fx;
  const auto path = fx.typed();
  const auto before = counts::counts();
  const Slab four{{.start = 0, .count = 4}};
  for (int i = 0; i < 1000; ++i) {
    const File file = open(path);
    // v4 returned early from each of these without nc_close.
    switch (i % 5) {
      case 0:
        CHECK(not file.read<double>("nope", four));
        break;
      case 1:
        CHECK(not file.read<float>("v_double", four));
        break;
      case 2:
        CHECK(not file.text_att("nope", "units"));
        break;
      case 3:
        CHECK(not file.read_samples("v_int64", four));
        break;
      default:
        CHECK(not open(path, ReadLimits{.max_att_bytes = 1})
                      .text_att(global, "nope")
                      .value()
                      .has_value());
        break;
    }
  }
  const auto& now = counts::counts();
  CHECK(now.opened - before.opened == 1200);
  CHECK(now.closed - before.closed == 1200);
  CHECK(now.close_calls - before.close_calls == 1200);
  CHECK(now.abort_calls == before.abort_calls);
}

TEST_CASE("char rows use the file's name length (B7, B11)",
          "[io][netcdf][regression][B7][B11]") {
  const Fixtures fx;
  // v4 read every row as substr(200 * i, 200).
  for (const std::size_t name_len : {20UZ, 50UZ, 64UZ, 300UZ}) {
    CAPTURE(name_len);
    const std::vector<std::string> rows{
        padded("Station One", name_len), padded("Grand Isle, LA", name_len),
        padded(std::string(name_len, 'x'), name_len)};
    const auto path = fx.path("names" + std::to_string(name_len) + ".nc");
    ncgen::make_char_rows(path, name_len, rows);
    const File file = open(path);
    const std::vector<std::string> read =
        value_of(file.read_char_rows("station_name"));
    CHECK(read == rows);
    for (const std::string& row : read) {
      CHECK(row.size() == name_len);
    }
  }
}
