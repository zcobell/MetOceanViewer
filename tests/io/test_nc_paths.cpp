// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Paths netCDF-C must be able to open on every OS (design section 4.1):
// non-ASCII names and paths longer than Windows' 260 characters.

#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <filesystem>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

#include "internal.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/test/scratch_dir.hpp"
#include "nc_test_helpers.hpp"

namespace {

using mov::io::Error;
using mov::io::nc::DimInfo;
using mov::io::nc::File;
using mov::io::nc::NewFile;
using mov::io::nc::write_netcdf_atomic;
using mov::test::ScratchDir;
using mov::test::nc::ReadLimits;
using mov::test::nc::value_of;

std::expected<void, Error> one_variable(NewFile& f) {
  return f.define_dim("n", 2)
      .and_then([&](const DimInfo& n) {
        const std::vector<DimInfo> dims{n};
        return f.define_var<double>("v", dims, {});
      })
      .and_then([&](const auto&) {
        const std::vector<double> values{1, 2};
        return f.put<double>("v", values, {{.start = 0, .count = 2}});
      })
      .transform_error(mov::io::lift<Error>);
}

// Whether the platform's file system may refuse the path itself.
enum class Refusal { never, apfs_non_utf8 };

// Writes, reads back and checks a file at `path` (its directory must exist).
void round_trip(const std::filesystem::path& path,
                [[maybe_unused]] const Refusal refusal = Refusal::never) {
  const auto written = write_netcdf_atomic(path, ReadLimits{}, one_variable);
#if defined(_WIN32)
  // netCDF-C reads paths in the active code page; a path it cannot express
  // must be refused cleanly (not yet verified on Windows).
  if (not written) {
    const auto* nc = std::get_if<mov::io::NcError>(&written.error());
    REQUIRE(nc != nullptr);
    WARN("not opened on Windows: " << path.string());
    return;
  }
#endif
#if defined(__APPLE__)
  // APFS rejects a file name that is not valid UTF-8 (EILSEQ), so there the
  // write must fail with an error and leave no file; every other path must
  // still round-trip.
  if (refusal == Refusal::apfs_non_utf8 and not written) {
    WARN("refused by the file system, as APFS does: " << path.string());
    std::error_code ec;  // stat of such a name may itself fail with EILSEQ
    CHECK_FALSE(std::filesystem::exists(path, ec));
    return;
  }
#endif
  REQUIRE(written.has_value());
  REQUIRE(std::filesystem::exists(path));
  const File file = value_of(File::open(path, ReadLimits{}));
  CHECK(value_of(file.read<double>("v", {{.start = 0, .count = 2}})) ==
        std::vector<double>{1, 2});
}

}  // namespace

TEST_CASE("a non-ASCII path", "[io][netcdf]") {
  const ScratchDir dir;
  // "données-é✓" in UTF-8, in a directory "Größe".
  const std::filesystem::path sub =
      dir.path() / std::filesystem::path{u8"Größe"};
  std::filesystem::create_directory(sub);
  round_trip(sub / std::filesystem::path{u8"données-✓.nc"});
}

TEST_CASE("a path longer than 260 characters", "[io][netcdf]") {
  const ScratchDir dir;
  std::filesystem::path deep = dir.path();
  while (deep.native().size() < 300) {
    deep /= std::string(40, 'd');
  }
  std::filesystem::create_directories(deep);
  const auto path = deep / "long-path.nc";
  REQUIRE(path.native().size() > 260);
  round_trip(path);
}

TEST_CASE("nc_path passes POSIX bytes unchanged", "[io][netcdf][posix]") {
#if defined(_WIN32)
  SKIP("POSIX only");
#else
  // Not UTF-8: a Latin-1 byte; a POSIX file name is bytes.
  const std::filesystem::path latin1{"\xe9t\xe9.nc"};
  CHECK(mov::io::nc::detail::nc_path(latin1).value() == "\xe9t\xe9.nc");
  const ScratchDir dir;
  round_trip(dir.path() / latin1, Refusal::apfs_non_utf8);
#endif
}
