// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// write_netcdf_atomic and the define/put side of nc::File (C16, B15, B19).

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

#include "mov/io/detail/atomic_file.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/test/scratch_dir.hpp"
#include "nc_counts.hpp"
#include "nc_test_helpers.hpp"

namespace {

using namespace std::literals;
using mov::io::Error;
using mov::io::FileError;
using mov::io::FileOp;
using mov::io::FormatErrc;
using mov::io::FormatError;
using mov::io::detail::AtomicStage;
using mov::io::detail::FaultInjector;
using mov::io::nc::DimInfo;
using mov::io::nc::File;
using mov::io::nc::global;
using mov::io::nc::Slab;
using mov::io::nc::VarInfo;
using mov::io::nc::VarOptions;
using mov::io::nc::write_netcdf_atomic;
using mov::io::nc::detail::write_netcdf_atomic_impl;
using mov::test::entry_names;
using mov::test::read_bytes;
using mov::test::ScratchDir;
using mov::test::write_bytes;
using mov::test::nc::error_of;
using mov::test::nc::LibraryStatus;
using mov::test::nc::NcError;
using mov::test::nc::NcOp;
using mov::test::nc::NcStatus;
using mov::test::nc::open;
using mov::test::nc::status_of;
using mov::test::nc::WrapperFault;
namespace ncgen = mov::test::ncgen;
namespace counts = mov::test::nc_counts;

constexpr int nc_format_netcdf4 = 3;  // NC_FORMAT_NETCDF4
constexpr int nc_double = 6;          // NC_DOUBLE
constexpr int nc_char = 2;            // NC_CHAR
constexpr int nc_eperm = -37;         // NC_EPERM
constexpr int nc_ehdferr = -101;      // NC_EHDFERR

// A small station file: dims station = 2, name_len = 12, time = 3.
std::expected<void, Error> station_file(File& f) {
  const auto station = f.define_dim("station", 2);
  const auto name_len = f.define_dim("name_len", 12);
  const auto time = f.define_dim("time", 3);
  if (not station or not name_len or not time) {
    return std::unexpected{Error{station.error()}};
  }
  const std::array<DimInfo, 2> names{*station, *name_len};
  const std::array<DimInfo, 2> data{*station, *time};
  return f.define_char_var("station_name", names)
      .and_then([&](const VarInfo&) {
        return f.define_var<double>(
            "zeta", data,
            VarOptions<double>{.fill = -99999.0,
                               .deflate_level = 2,
                               .chunks = std::vector<std::size_t>{1, 3}});
      })
      .and_then(
          [&](const VarInfo&) { return f.put_att("zeta", "units", "m"sv); })
      .and_then([&] {
        return f.put_att(global, "title", "Gen\xc3\xa8ve \xe2\x9c\x93"sv);
      })
      .and_then([&] {
        const std::array<std::int32_t, 1> epsg{4326};
        return f.put_att<std::int32_t>(global, "epsg", epsg);
      })
      .and_then([&] { return f.end_define(); })
      .and_then([&] {
        const std::array<std::string, 2> rows{"Grand Isle"s, "\xc3\xa9"s};
        return f.put_char_rows("station_name", rows);
      })
      .and_then([&] {
        const std::array<double, 6> values{1, 2, 3, 4, 5, 6};
        return f.put<double>(
            "zeta", values,
            {{.start = 0, .count = 2}, {.start = 0, .count = 3}});
      })
      .transform_error(mov::io::lift<Error>);
}

// Must not be called.
std::expected<void, Error> fail_test(File&) {
  throw std::logic_error{"the body must not run"};
}

}  // namespace

TEST_CASE("write_netcdf_atomic writes a netCDF-4 file", "[io][netcdf]") {
  const ScratchDir dir;
  const auto target = dir / "out.nc";
  REQUIRE(write_netcdf_atomic(target, station_file).has_value());
  CHECK(entry_names(dir.path()) == std::vector<std::string>{"out.nc"});
  CHECK(ncgen::raw_format(target) == nc_format_netcdf4);
  CHECK(ncgen::raw_var_type(target, "zeta") == nc_double);
  CHECK(ncgen::raw_double_fill(target, "zeta") == -99999.0);
  const auto storage = ncgen::raw_storage(target, "zeta");
  CHECK(storage.shuffle == 1);
  CHECK(storage.deflate == 1);
  CHECK(storage.level == 2);
  CHECK(storage.chunks == std::vector<std::size_t>{1, 3});
  CHECK(ncgen::raw_doubles(target, "zeta") ==
        std::vector<double>{1, 2, 3, 4, 5, 6});
  // NUL-padded rows of the byte length (B15).
  CHECK(ncgen::raw_chars(target, "station_name") ==
        "Grand Isle\0\0"s + "\xc3\xa9\0\0\0\0\0\0\0\0\0\0"s);
  // Read back through the wrapper.
  const File file = open(target);
  CHECK(file.text_att(global, "title").value() == "Gen\xc3\xa8ve \xe2\x9c\x93");
  CHECK(file.numeric_att<std::int32_t>(global, "epsg").value() ==
        std::vector<std::int32_t>{4326});
}

TEST_CASE("put_att and put_char_rows write byte lengths (B15)",
          "[io][netcdf][regression][B15]") {
  const ScratchDir dir;
  const auto target = dir / "b15.nc";
  // "Gen\u00e8ve \u2713" is 8 characters and 11 bytes; v4 wrote
  // QString::length().
  REQUIRE(write_netcdf_atomic(target, station_file).has_value());
  const auto title = ncgen::raw_att(target, "", "title");
  CHECK(title.xtype == nc_char);
  CHECK(title.length == 11);
  CHECK(title.bytes == "Gen\xc3\xa8ve \xe2\x9c\x93");
  CHECK(ncgen::raw_att(target, "zeta", "units").length == 1);
  // A row longer than the dimension is refused, not cut or over-read.
  const auto too_long = write_netcdf_atomic(dir / "long.nc", [](File& f) {
    const auto station = f.define_dim("station", 1).value();
    const auto len = f.define_dim("name_len", 4).value();
    const std::array<DimInfo, 2> dims{station, len};
    const std::array<std::string, 1> rows{"Grand"s};
    return f.define_char_var("name", dims).and_then([&](const VarInfo&) {
      return f.put_char_rows("name", rows);
    });
  });
  CHECK(status_of(error_of(too_long)) == NcStatus{WrapperFault::name_too_long});
  CHECK(entry_names(dir.path()) == std::vector<std::string>{"b15.nc"});
}

TEST_CASE("the define and put checks", "[io][netcdf][regression][B4]") {
  const ScratchDir dir;
  const auto result = write_netcdf_atomic(dir / "x.nc", [](File& f) {
    CHECK(error_of(f.define_dim("t", 0)) ==
          NcError{.status = WrapperFault::zero_length_dim,
                  .op = NcOp::def_dim,
                  .object = "t",
                  .file = f.path()});
    const DimInfo n = f.define_dim("n", 3).value();
    const std::array<DimInfo, 1> dims{n};
    REQUIRE(f.define_var<double>("d", dims, {}).has_value());
    REQUIRE(f.define_var<float>("f", dims, {}).has_value());
    REQUIRE(f.define_char_var("c", dims).has_value());
    CHECK(error_of(
              f.define_var<double>("chunked", dims,
                                   {.chunks = std::vector<std::size_t>{1, 1}}))
              .status == NcStatus{WrapperFault::rank_mismatch});
    const Slab all{{.start = 0, .count = 3}};
    const std::array<float, 3> floats{1, 2, 3};
    const std::array<double, 3> doubles{1, 2, 3};
    const std::array<double, 2> two{1, 2};
    // B4: a float buffer is never written into a double variable.
    CHECK(error_of(f.put<float>("d", floats, all)).status ==
          NcStatus{WrapperFault::type_mismatch});
    CHECK(error_of(f.put<double>("d", two, all)).status ==
          NcStatus{WrapperFault::count_mismatch});
    CHECK(error_of(f.put<double>("d", doubles, {})).status ==
          NcStatus{WrapperFault::rank_mismatch});
    CHECK(error_of(f.put_char_rows("d", {})).status ==
          NcStatus{WrapperFault::type_mismatch});
    CHECK(error_of(f.put_char_rows("c", {})).status ==
          NcStatus{WrapperFault::rank_mismatch});
    CHECK(error_of(f.put_att(global, "big",
                             std::string(
                                 mov::io::ReadLimits{}.max_att_bytes + 1, 'x')))
              .status == NcStatus{WrapperFault::too_large});
    CHECK(error_of(f.put_att("nope", "units", "m"sv)).status ==
          NcStatus{LibraryStatus{-49}});  // NC_ENOTVAR
    CHECK(f.put<float>("f", floats, all).has_value());
    CHECK(f.put<double>("d", doubles, all).has_value());
    return std::expected<void, Error>{};
  });
  CHECK(result.has_value());
}

TEST_CASE("a read handle cannot write", "[io][netcdf]") {
  mov::test::nc::Fixtures fx;
  File file = open(fx.typed());
  CHECK(error_of(file.define_dim("new", 2)).status ==
        NcStatus{LibraryStatus{nc_eperm}});
  CHECK(error_of(file.put_att(global, "title", "x"sv)).status ==
        NcStatus{LibraryStatus{nc_eperm}});
}

TEST_CASE("a failed body leaves the target and no temporary file",
          "[io][netcdf]") {
  const ScratchDir dir;
  const auto target = dir / "out.nc";
  write_bytes(target, "old content");
  const FormatError refusal{.code = FormatErrc::empty_collection,
                            .subject = "stations"};
  const auto result = write_netcdf_atomic(
      target, [&](File& f) -> std::expected<void, FormatError> {
        REQUIRE(f.define_dim("n", 3).has_value());
        return std::unexpected{refusal};
      });
  CHECK(error_of(result) == Error{refusal});
  CHECK(read_bytes(target) == "old content");
  CHECK(entry_names(dir.path()) == std::vector<std::string>{"out.nc"});
  // A body that throws.
  CHECK_THROWS_AS(static_cast<void>(write_netcdf_atomic(
                      target,
                      [](File& f) -> std::expected<void, Error> {
                        REQUIRE(f.define_dim("n", 3).has_value());
                        throw std::runtime_error{"body"};
                      })),
                  std::runtime_error);
  CHECK(read_bytes(target) == "old content");
  CHECK(entry_names(dir.path()) == std::vector<std::string>{"out.nc"});
}

TEST_CASE("a fault at every stage leaves the target as it was (B19)",
          "[io][netcdf][regression][B6]") {
  const ScratchDir dir;
  const auto target = dir / "out.nc";
  write_bytes(target, "old content");
  const std::error_code io_error = std::make_error_code(std::errc::io_error);
  const std::array<std::pair<AtomicStage, FileOp>, 5> stages{{
      {AtomicStage::create, FileOp::create},
      {AtomicStage::body, FileOp::write},
      {AtomicStage::close, FileOp::close},
      {AtomicStage::fsync_file, FileOp::fsync},
      {AtomicStage::rename, FileOp::rename},
  }};
  for (const auto& [stage, op] : stages) {
    CAPTURE(static_cast<int>(stage));
    const auto result = write_netcdf_atomic_impl(
        target, station_file, FaultInjector{.fail_at = stage});
    CHECK(error_of(result) ==
          Error{FileError{.op = op, .path = target, .ec = io_error}});
    CHECK(read_bytes(target) == "old content");
    CHECK(entry_names(dir.path()) == std::vector<std::string>{"out.nc"});
  }
  // The directory fsync fails after the rename: the target is new.
  const auto late = write_netcdf_atomic_impl(
      target, station_file, FaultInjector{.fail_at = AtomicStage::fsync_dir});
  CHECK(error_of(late) ==
        Error{FileError{
            .op = FileOp::fsync_dir, .path = target, .ec = io_error}});
  CHECK(ncgen::raw_format(target) == nc_format_netcdf4);
  CHECK(entry_names(dir.path()) == std::vector<std::string>{"out.nc"});
}

TEST_CASE("the temporary file is created exclusively (NC_NOCLOBBER)",
          "[io][netcdf]") {
  const ScratchDir dir;
  const auto target = dir / "out.nc";
  const FaultInjector fixed{.temp_suffix = 0xABCDEF};
  const auto temp = mov::io::detail::temp_path_for(target, fixed.temp_suffix);
  write_bytes(temp, "someone else's");
  const auto result = write_netcdf_atomic_impl(target, fail_test, fixed);
  const NcError error =
      mov::test::nc::must(mov::test::nc::nc_error_of(error_of(result)));
  CHECK(error.op == NcOp::create);
  CHECK(error.file == target);
  CHECK(read_bytes(temp) == "someone else's");
  CHECK(not std::filesystem::exists(target));
}

TEST_CASE("a read-only target is not replaced", "[io][netcdf][posix]") {
  const ScratchDir dir;
  const auto target = dir / "out.nc";
  write_bytes(target, "old content");
  std::filesystem::permissions(target, std::filesystem::perms::owner_read,
                               std::filesystem::perm_options::replace);
  const auto result = write_netcdf_atomic_impl(target, fail_test, {});
  CHECK(error_of(result) ==
        Error{FileError{
            .op = FileOp::open,
            .path = target,
            .ec = std::make_error_code(std::errc::permission_denied)}});
  CHECK(entry_names(dir.path()) == std::vector<std::string>{"out.nc"});
}

TEST_CASE("a close that fails is reported and leaves nothing behind",
          "[io][netcdf][linux]") {
  if constexpr (not counts::available) {
    SKIP("needs the --wrap shims (Linux)");
  }
  const ScratchDir dir;
  const auto target = dir / "out.nc";
  write_bytes(target, "old content");
  const auto before = counts::counts();
  counts::counts().fail_next_closes = 1;
  const auto result = write_netcdf_atomic(target, station_file);
  CHECK(error_of(result) == Error{NcError{.status = LibraryStatus{nc_ehdferr},
                                          .op = NcOp::close,
                                          .object = {},
                                          .file = target}});
  CHECK(read_bytes(target) == "old content");
  CHECK(entry_names(dir.path()) == std::vector<std::string>{"out.nc"});
  CHECK(counts::counts().abort_calls == before.abort_calls + 1);
}
