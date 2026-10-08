// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The nc_call choke point, the build's netCDF-C, and the open/close counts.

#include <netcdf_meta.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include "mov/io/error.hpp"
#include "mov/io/netcdf_library.hpp"
#include "nc_call.hpp"
#include "nc_counts.hpp"
#include "nc_test_helpers.hpp"

#if !defined(_WIN32)
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#endif

namespace {

using mov::io::nc::detail::nc_call;
using mov::io::nc::detail::nc_status;
using mov::test::nc::error_of;
using mov::test::nc::LibraryStatus;
using mov::test::nc::NcError;
using mov::test::nc::NcOp;
namespace counts = mov::test::nc_counts;

}  // namespace

TEST_CASE("the netCDF-C of the build has netCDF-4/HDF5", "[io][netcdf]") {
  STATIC_REQUIRE(NC_HAS_HDF5 == 1);
  CHECK(mov::io::netcdf_library_version().starts_with("4.9.3"));
}

TEST_CASE("nc_call turns a status into an NcError", "[io][netcdf]") {
  CHECK(nc_status([] { return 42; }) == 42);
  CHECK(nc_call(NcOp::inquire, "zeta", "f.nc", [] { return 0; }).has_value());
  CHECK(error_of(nc_call(NcOp::get_var, "zeta", "f.nc", [] { return -49; })) ==
        NcError{.status = LibraryStatus{-49},
                .op = NcOp::get_var,
                .object = "zeta",
                .file = "f.nc"});
  // Sequential calls are fine; only nesting is refused.
  CHECK(nc_status([] { return 0; }) == 0);
}

TEST_CASE("nc_call asserts on re-entry in debug builds",
          "[io][netcdf][linux]") {
#if defined(NDEBUG) or defined(_WIN32)
  SKIP("needs assert() (a debug build) and fork()");
#else
  const pid_t child = ::fork();
  REQUIRE(child >= 0);
  if (child == 0) {
    // The child: a netCDF call made from inside another must abort. Catch2's
    // handler would report the abort as a failure of the child; the default
    // action ends it with the signal the parent checks.
    // No core file either.
    static_cast<void>(std::signal(SIGABRT, SIG_DFL));
    const rlimit no_core{.rlim_cur = 0, .rlim_max = 0};
    static_cast<void>(::setrlimit(RLIMIT_CORE, &no_core));
    static_cast<void>(nc_status([] { return nc_status([] { return 0; }); }));
    std::_Exit(EXIT_SUCCESS);
  }
  int status = 0;
  REQUIRE(::waitpid(child, &status, 0) == child);
  CHECK(WIFSIGNALED(status));
  CHECK(WTERMSIG(status) == SIGABRT);
#endif
}

TEST_CASE("the open and close counts see the wrapper", "[io][netcdf][linux]") {
  if constexpr (not counts::available) {
    SKIP("needs the --wrap shims (Linux)");
  }
  mov::test::nc::Fixtures fx;
  const auto path = fx.typed();
  const auto before = counts::counts();
  {
    const auto file = mov::test::nc::open(path);
    CHECK(counts::counts().opened == before.opened + 1);
  }
  CHECK(counts::counts().closed == before.closed + 1);
}

// ---- one handle per file (File::open) ---------------------------------------

#if !defined(NDEBUG) and !defined(_WIN32)
namespace {

// How a child process ended, and what it wrote to its standard error. The
// child's abort must be the default action (Catch2's handler would report it
// as a failure of the child) and leave no core file.
enum class Ending { exited, aborted };

struct ChildResult {
  Ending ending;
  std::string error_output;
};

template <class Body>
ChildResult run_in_child(const Body& body) {
  std::array<int, 2> pipe_ends{};
  REQUIRE(::pipe(pipe_ends.data()) == 0);
  const pid_t child = ::fork();
  REQUIRE(child >= 0);
  if (child == 0) {
    ::close(pipe_ends[0]);
    ::dup2(pipe_ends[1], STDERR_FILENO);
    static_cast<void>(std::signal(SIGABRT, SIG_DFL));
    const rlimit no_core{.rlim_cur = 0, .rlim_max = 0};
    static_cast<void>(::setrlimit(RLIMIT_CORE, &no_core));
    body();
    std::_Exit(EXIT_SUCCESS);
  }
  ::close(pipe_ends[1]);
  std::string output;
  std::array<char, 512> chunk{};
  for (ssize_t n = ::read(pipe_ends[0], chunk.data(), chunk.size()); n > 0;
       n = ::read(pipe_ends[0], chunk.data(), chunk.size())) {
    output.append(chunk.data(), static_cast<std::size_t>(n));
  }
  ::close(pipe_ends[0]);
  int status = 0;
  REQUIRE(::waitpid(child, &status, 0) == child);
  if (WIFSIGNALED(status) and WTERMSIG(status) == SIGABRT) {
    return {.ending = Ending::aborted, .error_output = std::move(output)};
  }
  REQUIRE(WIFEXITED(status));
  REQUIRE(WEXITSTATUS(status) == EXIT_SUCCESS);
  return {.ending = Ending::exited, .error_output = std::move(output)};
}

// An abort for the reason under test, not for another.
bool aborted_for_a_second_handle(const ChildResult& result) {
#if defined(__linux__)
  // glibc's assert prints the text of the failed condition.
  return result.ending == Ending::aborted and
         result.error_output.contains("already open through another nc::File");
#else
  return result.ending == Ending::aborted;  // other libcs print other text
#endif
}

}  // namespace
#endif

TEST_CASE("a second handle on an open file asserts in debug builds",
          "[io][netcdf][linux]") {
#if defined(NDEBUG) or defined(_WIN32)
  SKIP("needs assert() (a debug build) and fork()");
#else
  using mov::io::nc::File;
  mov::test::nc::Fixtures fx;
  const auto path = fx.typed();

  SECTION("the same path") {
    CHECK(aborted_for_a_second_handle(run_in_child([&] {
      const auto first = File::open(path, {});
      const auto second = File::open(path, {});
    })));
  }
  SECTION("another spelling of the path") {
    const auto alias = fx.dir() / "." / "typed.nc";
    CHECK(aborted_for_a_second_handle(run_in_child([&] {
      const auto first = File::open(path, {});
      const auto second = File::open(alias, {});
    })));
  }
  SECTION("a symbolic link and a hard link") {
    const auto symlink = fx.dir() / "symlink.nc";
    const auto hardlink = fx.dir() / "hardlink.nc";
    std::filesystem::create_symlink(path, symlink);
    std::filesystem::create_hard_link(path, hardlink);
    CHECK(aborted_for_a_second_handle(run_in_child([&] {
      const auto first = File::open(path, {});
      const auto second = File::open(symlink, {});
    })));
    CHECK(aborted_for_a_second_handle(run_in_child([&] {
      const auto first = File::open(path, {});
      const auto second = File::open(hardlink, {});
    })));
  }
  SECTION("a moved handle still counts as open") {
    CHECK(aborted_for_a_second_handle(run_in_child([&] {
      auto first = File::open(path, {});
      const auto moved = std::move(first);
      const auto second = File::open(path, {});
    })));
  }
  SECTION("the working directory does not matter") {
    // Two different files with one relative name: no abort.
    const auto one = fx.dir() / "one";
    const auto two = fx.dir() / "two";
    std::filesystem::create_directory(one);
    std::filesystem::create_directory(two);
    std::filesystem::copy_file(path, one / "x.nc");
    std::filesystem::copy_file(path, two / "x.nc");
    const auto different = run_in_child([&] {
      std::filesystem::current_path(one);
      const auto first = File::open("x.nc", {});
      std::filesystem::current_path(two);
      const auto second = File::open("x.nc", {});
      if (not first or not second) {
        std::_Exit(EXIT_FAILURE);
      }
    });
    CHECK(different.ending == Ending::exited);
    // One file, named relative to another directory than the first time: abort.
    CHECK(aborted_for_a_second_handle(run_in_child([&] {
      const auto first = File::open(path, {});
      std::filesystem::current_path(fx.dir().parent_path());
      const auto second = File::open(fx.dir().filename() / "typed.nc", {});
    })));
    // And the first spelled relative, the second absolute, from elsewhere.
    CHECK(aborted_for_a_second_handle(run_in_child([&] {
      std::filesystem::current_path(fx.dir());
      const auto first = File::open("typed.nc", {});
      std::filesystem::current_path("/");
      const auto second = File::open(path, {});
    })));
  }
  SECTION("a closed or destroyed handle is gone") {
    CHECK(run_in_child([&] {
            auto first = File::open(path, {});
            if (not std::move(*first).close()) {
              std::_Exit(EXIT_FAILURE);
            }
            const auto second = File::open(path, {});
            if (not second) {
              std::_Exit(EXIT_FAILURE);
            }
          }).ending == Ending::exited);
    CHECK(run_in_child([&] {
            {
              const auto first = File::open(path, {});
            }
            const auto second = File::open(path, {});
            if (not second) {
              std::_Exit(EXIT_FAILURE);
            }
          }).ending == Ending::exited);
  }
  SECTION("different files, and a failed open, are fine") {
    const auto other = fx.masking();
    const auto missing = fx.path("missing.nc");
    CHECK(run_in_child([&] {
            const auto first = File::open(path, {});
            const auto second = File::open(other, {});
            const auto absent = File::open(missing, {});
            const auto again = File::open(missing, {});
            if (not second or absent or again) {
              std::_Exit(EXIT_FAILURE);
            }
          }).ending == Ending::exited);
  }
#endif
}
