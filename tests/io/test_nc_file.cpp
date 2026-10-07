// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// nc::File: open, close, moves, the close policy and structure queries.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "nc_counts.hpp"
#include "nc_test_helpers.hpp"

#if !defined(_WIN32)
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include <cerrno>

namespace {

using mov::io::nc::DimInfo;
using mov::io::nc::File;
using mov::io::nc::NcName;
using mov::io::nc::Type;
using mov::io::nc::VarInfo;
using mov::test::nc::error_of;
using mov::test::nc::Fixtures;
using mov::test::nc::LibraryStatus;
using mov::test::nc::must;
using mov::test::nc::NcError;
using mov::test::nc::NcOp;
using mov::test::nc::NcStatus;
using mov::test::nc::open;
using mov::test::nc::ReadLimits;
using mov::test::nc::value_of;
using mov::test::nc::WrapperFault;
namespace counts = mov::test::nc_counts;

constexpr int nc_enoent = ENOENT;
constexpr int nc_enotnc = -51;    // NC_ENOTNC
constexpr int nc_ehdferr = -101;  // NC_EHDFERR

NcName name(std::string_view text) { return NcName::make(text).value(); }

}  // namespace

TEST_CASE("open: a missing file is an error and nothing is closed (B6)",
          "[io][netcdf][regression][B6]") {
  const Fixtures fx;
  const auto before = counts::counts();
  const auto missing = File::open(fx.path("missing.nc"), ReadLimits{});
  REQUIRE(not missing.has_value());
  CHECK(missing.error() == NcError{.status = LibraryStatus{nc_enoent},
                                   .op = NcOp::open,
                                   .object = {},
                                   .file = fx.path("missing.nc")});
  // v4's NCCHECK called nc_close on the uninitialised id here.
  CHECK(counts::counts().close_calls == before.close_calls);
  CHECK(counts::counts().abort_calls == before.abort_calls);
}

TEST_CASE("open refuses what is not a netCDF regular file", "[io][netcdf]") {
  const Fixtures fx;
  mov::test::ncgen::make_not_netcdf(fx.path("text.nc"));
  CHECK(error_of(File::open(fx.path("text.nc"), ReadLimits{})).status ==
        NcStatus{LibraryStatus{nc_enotnc}});
  CHECK(error_of(File::open(fx.dir(), ReadLimits{})).status ==
        NcStatus{LibraryStatus{EISDIR}});
#if !defined(_WIN32)
  // A FIFO would block open(); it is refused before netCDF-C sees it.
  const auto fifo = fx.path("fifo.nc");
  REQUIRE(::mkfifo(fifo.c_str(), 0600) == 0);
  CHECK(error_of(File::open(fifo, ReadLimits{})).status ==
        NcStatus{LibraryStatus{EINVAL}});
#endif
}

TEST_CASE("close reports, and a closed or moved-from File is closed",
          "[io][netcdf]") {
  Fixtures fx;
  File file = open(fx.typed());
  CHECK(file.is_open());
  CHECK(file.path() == fx.typed());
  File moved = std::move(file);
  // The moved-from and the closed state are defined: closed.
  // NOLINTBEGIN(bugprone-use-after-move)
  CHECK(not file.is_open());
  const NcError closed{.status = WrapperFault::closed,
                       .op = NcOp::inquire,
                       .object = "v_double",
                       .file = fx.typed()};
  CHECK(error_of(file.find_var("v_double")).status ==
        NcStatus{WrapperFault::closed});
  const auto first = std::move(moved).close();
  CHECK(first.has_value());
  CHECK(error_of(moved.find_var("v_double")) == closed);
  const auto second = std::move(moved).close();
  CHECK(error_of(second).status == NcStatus{WrapperFault::closed});
  // NOLINTEND(bugprone-use-after-move)
}

TEST_CASE("move assignment closes the file it replaces", "[io][netcdf]") {
  Fixtures fx;
  File a = open(fx.typed());
  File b = open(fx.masking());
  const auto before = counts::counts();
  a = std::move(b);
  CHECK(a.find_var("d_fill").value().has_value());
  if constexpr (counts::available) {
    CHECK(counts::counts().closed == before.closed + 1);
  }
}

TEST_CASE("a failed close is reported and the id is still released",
          "[io][netcdf][linux]") {
  if constexpr (not counts::available) {
    SKIP("needs the --wrap shims (Linux)");
  }
  Fixtures fx;
  const auto before = counts::counts();
  File file = open(fx.typed());
  counts::counts().fail_next_closes = 1;
  const auto failed = std::move(file).close();
  CHECK(error_of(failed) == NcError{.status = LibraryStatus{nc_ehdferr},
                                    .op = NcOp::close,
                                    .object = {},
                                    .file = fx.typed()});
  CHECK(counts::counts().abort_calls == before.abort_calls + 1);
  CHECK(counts::counts().opened - before.opened ==
        counts::counts().closed - before.closed);

  // The destructor's close fails too: it aborts instead.
  {
    const File dropped = open(fx.typed());
    counts::counts().fail_next_closes = 1;
  }
  CHECK(counts::counts().abort_calls == before.abort_calls + 2);
  CHECK(counts::counts().opened - before.opened ==
        counts::counts().closed - before.closed);
}

TEST_CASE("find_dim and find_var: absent is nullopt, never id 0 (B12)",
          "[io][netcdf][regression][B12]") {
  Fixtures fx;
  const File file = open(fx.typed());
  CHECK(file.find_dim("nope").value() == std::nullopt);
  CHECK(file.find_var("nope").value() == std::nullopt);
  const DimInfo n = must(file.find_dim("n").value());
  CHECK(n == DimInfo{.id = 0, .name = name("n"), .length = 4});
  const DimInfo cols = must(file.find_dim("cols").value());
  CHECK(cols.id != 0);
  CHECK(cols.length == 4);

  const VarInfo grid = must(file.find_var("grid").value());
  CHECK(grid.type == Type::double_);
  REQUIRE(grid.dims.size() == 2);
  CHECK(grid.dims[0].name.view() == "rows");
  CHECK(grid.dims[0].length == 3);
  CHECK(grid.dims[1].name.view() == "cols");
  const VarInfo scalar = must(file.find_var("s_double").value());
  CHECK(scalar.dims.empty());
}

TEST_CASE("variables lists every variable with its type", "[io][netcdf]") {
  Fixtures fx;
  const File file = open(fx.typed());
  const std::vector<VarInfo> vars = value_of(file.variables());
  std::vector<std::pair<std::string, Type>> seen;
  seen.reserve(vars.size());
  for (const VarInfo& v : vars) {
    seen.emplace_back(std::string{v.name.view()}, v.type);
  }
  const std::vector<std::pair<std::string, Type>> expected{
      {"v_double", Type::double_}, {"v_float", Type::float_},
      {"v_byte", Type::byte},      {"v_short", Type::short_},
      {"v_int", Type::int_},       {"v_int64", Type::int64},
      {"v_ubyte", Type::ubyte},    {"v_ushort", Type::ushort},
      {"v_uint", Type::uint},      {"v_uint64", Type::uint64},
      {"s_double", Type::double_}, {"v_char", Type::char_},
      {"v_string", Type::string},  {"v_opaque", Type::other},
      {"grid", Type::double_},     {"cube", Type::int_},
  };
  CHECK(seen == expected);
  for (std::size_t i = 0; i < vars.size(); ++i) {
    CHECK(std::cmp_equal(vars[i].id, i));
  }
}

TEST_CASE("the time dimension need not be dimension 0 (B12)",
          "[io][netcdf][regression][B12]") {
  Fixtures fx;
  const File file = open(fx.hostile(mov::test::ncgen::Hostile::time_not_first));
  CHECK(must(file.find_dim("time").value()).id == 1);
  CHECK(must(file.find_dim("station").value()).id == 0);
  const VarInfo time = must(file.find_var("time").value());
  CHECK(time.id == 1);
  CHECK(time.dims.front().name.view() == "time");
}
