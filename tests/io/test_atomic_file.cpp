// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "mov/io/detail/atomic_file.hpp"
#include "mov/io/error.hpp"
#include "mov/io/text_file.hpp"
#include "mov/test/scratch_dir.hpp"

namespace {

using namespace std::string_literals;
using mov::io::FileError;
using mov::io::FileOp;
using mov::io::detail::AtomicStage;
using mov::io::detail::FaultInjector;
using mov::test::entry_names;
using mov::test::read_bytes;
using mov::test::ScratchDir;
using mov::test::write_bytes;

const std::error_code io_error = std::make_error_code(std::errc::io_error);

auto writing(std::string_view text) {
  return [text](std::ostream& out) { out << text; };
}

bool can_open_for_write(const std::filesystem::path& path) {
  std::ofstream out{path, std::ios::app | std::ios::binary};
  return out.is_open();
}

// Changes the working directory for its lifetime.
class ScopedCurrentDirectory {
 public:
  explicit ScopedCurrentDirectory(const std::filesystem::path& dir)
      : previous_{std::filesystem::current_path()} {
    std::filesystem::current_path(dir);
  }
  ScopedCurrentDirectory(const ScopedCurrentDirectory&) = delete;
  ScopedCurrentDirectory& operator=(const ScopedCurrentDirectory&) = delete;
  ScopedCurrentDirectory(ScopedCurrentDirectory&&) = delete;
  ScopedCurrentDirectory& operator=(ScopedCurrentDirectory&&) = delete;
  ~ScopedCurrentDirectory() {
    std::error_code ignored;
    std::filesystem::current_path(previous_, ignored);
  }

 private:
  std::filesystem::path previous_;
};

}  // namespace

TEST_CASE("write_file_atomic creates a file", "[io][atomic]") {
  const ScratchDir dir;
  const auto target = dir / "out.txt";
  const auto result = mov::io::write_file_atomic(target, writing("hello\n"));
  REQUIRE(result.has_value());
  CHECK(read_bytes(target) == "hello\n");
  CHECK(entry_names(dir.path()) == std::vector<std::string>{"out.txt"});
}

TEST_CASE("write_file_atomic replaces an existing file", "[io][atomic]") {
  const ScratchDir dir;
  const auto target = dir / "out.txt";
  write_bytes(target, "old content that is longer than the new one");
  REQUIRE(mov::io::write_file_atomic(target, writing("new")).has_value());
  CHECK(read_bytes(target) == "new");
  CHECK(entry_names(dir.path()) == std::vector<std::string>{"out.txt"});
}

TEST_CASE("write_file_atomic writes bytes, not translated text",
          "[io][atomic]") {
  const ScratchDir dir;
  const auto target = dir / "out.bin";
  const std::string bytes = "a\r\nb\0c\n\xEF\xBB\xBF"s;
  REQUIRE(mov::io::write_file_atomic(target, [&bytes](std::ostream& out) {
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
          }).has_value());
  CHECK(read_bytes(target) == bytes);
}

TEST_CASE("write_file_atomic can write an empty file", "[io][atomic]") {
  const ScratchDir dir;
  const auto target = dir / "empty.txt";
  write_bytes(target, "something");
  REQUIRE(mov::io::write_file_atomic(target, [](std::ostream&) {}).has_value());
  CHECK(read_bytes(target).empty());
  CHECK(std::filesystem::exists(target));
}

TEST_CASE("write_file_atomic accepts any callable with a stream",
          "[io][atomic]") {
  struct Writer {
    void operator()(std::ostream& out) const { out << "functor"; }
  };
  const ScratchDir dir;
  const auto target = dir / "out.txt";
  const Writer writer;
  REQUIRE(mov::io::write_file_atomic(target, writer).has_value());
  CHECK(read_bytes(target) == "functor");
  REQUIRE(mov::io::write_file_atomic(target, Writer{}).has_value());
  REQUIRE(mov::io::write_file_atomic(
              target, std::function<void(std::ostream&)>{writing("fn")})
              .has_value());
  CHECK(read_bytes(target) == "fn");
}

TEST_CASE("write_file_atomic works for a bare file name", "[io][atomic]") {
  const ScratchDir dir;
  const ScopedCurrentDirectory cd{dir.path()};
  REQUIRE(mov::io::write_file_atomic("bare.txt", writing("x")).has_value());
  CHECK(read_bytes(dir / "bare.txt") == "x");
  CHECK(entry_names(dir.path()) == std::vector<std::string>{"bare.txt"});
}

TEST_CASE("a stream that fails during the body fails the write",
          "[io][atomic]") {
  const ScratchDir dir;
  const auto target = dir / "out.txt";
  write_bytes(target, "precious");
  const auto result = mov::io::write_file_atomic(target, [](std::ostream& out) {
    out << "partial";
    out.setstate(std::ios::failbit);
  });
  REQUIRE(not result.has_value());
  CHECK(result.error().op == FileOp::write);
  CHECK(result.error().path == target);
  CHECK(read_bytes(target) == "precious");
  CHECK(entry_names(dir.path()) == std::vector<std::string>{"out.txt"});
}

TEST_CASE("a body that throws leaves the target and the directory untouched",
          "[io][atomic]") {
  const ScratchDir dir;
  const auto target = dir / "out.txt";
  write_bytes(target, "precious");
  const auto throwing = [](std::ostream& out) {
    out << "partial";
    throw std::runtime_error{"body failed"};
  };
  CHECK_THROWS_AS(
      static_cast<void>(mov::io::write_file_atomic(target, throwing)),
      std::runtime_error);
  CHECK(read_bytes(target) == "precious");
  CHECK(entry_names(dir.path()) == std::vector<std::string>{"out.txt"});
}

// Every stage can fail. Before the rename the target is byte-identical to what
// it was (B19) and the directory holds no temporary file; a failure of the
// directory fsync comes after the rename, so the target is the complete new
// file, but the error is still reported.
TEST_CASE("fault injection at every stage", "[io][atomic][regression][B19]") {
  struct Case {
    AtomicStage stage;
    FileOp op;
    bool replaced;  // the target holds the new content afterwards
  };
  constexpr std::array cases{
      Case{.stage = AtomicStage::create,
           .op = FileOp::create,
           .replaced = false},
      Case{.stage = AtomicStage::body, .op = FileOp::write, .replaced = false},
      Case{.stage = AtomicStage::close, .op = FileOp::close, .replaced = false},
      Case{.stage = AtomicStage::fsync_file,
           .op = FileOp::fsync,
           .replaced = false},
      Case{.stage = AtomicStage::rename,
           .op = FileOp::rename,
           .replaced = false},
      Case{.stage = AtomicStage::fsync_dir,
           .op = FileOp::fsync,
           .replaced = true},
  };
  for (const Case& c : cases) {
    DYNAMIC_SECTION("stage " << static_cast<int>(c.stage)) {
      const ScratchDir dir;
      const auto target = dir / "out.txt";
      write_bytes(target, "old");
      const auto result = mov::io::detail::write_text_atomic(
          target, writing("new"), FaultInjector{.fail_at = c.stage});
      REQUIRE(not result.has_value());
      CHECK(result.error() ==
            FileError{.op = c.op, .path = target, .ec = io_error});
      CHECK(read_bytes(target) == (c.replaced ? "new" : "old"));
      CHECK(entry_names(dir.path()) == std::vector<std::string>{"out.txt"});
    }
  }
}

TEST_CASE("a failed write does not create a target that was absent",
          "[io][atomic][regression][B19]") {
  for (const AtomicStage stage :
       {AtomicStage::create, AtomicStage::body, AtomicStage::close,
        AtomicStage::fsync_file, AtomicStage::rename}) {
    const ScratchDir dir;
    const auto target = dir / "absent.txt";
    const auto result = mov::io::detail::write_text_atomic(
        target, writing("new"), FaultInjector{.fail_at = stage});
    REQUIRE(not result.has_value());
    CHECK(not std::filesystem::exists(target));
    CHECK(entry_names(dir.path()).empty());
  }
}

TEST_CASE("the injector with no stage lets the write succeed", "[io][atomic]") {
  const ScratchDir dir;
  const auto target = dir / "out.txt";
  REQUIRE(
      mov::io::detail::write_text_atomic(target, writing("ok"), FaultInjector{})
          .has_value());
  CHECK(read_bytes(target) == "ok");
}

TEST_CASE("the temporary file is created exclusively",
          "[io][atomic][regression][B19]") {
  const ScratchDir dir;
  const auto target = dir / "out.txt";
  write_bytes(target, "old");
  const FaultInjector fault{.temp_suffix = 0xABCDEF};
  const auto temp = mov::io::detail::temp_path_for(target, fault.temp_suffix);
  write_bytes(temp, "someone else's file");

  const auto result =
      mov::io::detail::write_text_atomic(target, writing("new"), fault);
  REQUIRE(not result.has_value());
  CHECK(result.error().op == FileOp::create);
  CHECK(result.error().path == target);
  CHECK(result.error().ec == std::make_error_code(std::errc::file_exists));
  // Neither the target nor the file that was in the way was touched or
  // removed.
  CHECK(read_bytes(target) == "old");
  CHECK(read_bytes(temp) == "someone else's file");
}

TEST_CASE("temporary file names are unique and next to the target",
          "[io][atomic]") {
  const std::filesystem::path target =
      std::filesystem::path{"some"} / "dir" / "out.txt";
  const auto first = mov::io::detail::temp_path_for(target, std::nullopt);
  const auto second = mov::io::detail::temp_path_for(target, std::nullopt);
  CHECK(first != second);
  for (const auto& temp : {first, second}) {
    CHECK(temp.parent_path() == target.parent_path());
    const std::string name = temp.filename().string();
    CHECK(name.starts_with("out.txt."));
    CHECK(name.ends_with(".tmp"));
    // "out.txt." + 16 hex digits + ".tmp"
    CHECK(name.size() == std::string_view{"out.txt."}.size() + 16 + 4);
  }
  CHECK(mov::io::detail::temp_path_for(target, 0x1F) ==
        target.string() + ".000000000000001f.tmp");
}

TEST_CASE("a target that is a directory fails the rename and leaves no temp",
          "[io][atomic]") {
  const ScratchDir dir;
  const auto target = dir / "occupied";
  std::filesystem::create_directory(target);
  write_bytes(target / "inside.txt", "keep");

  const auto result = mov::io::write_file_atomic(target, writing("new"));
  REQUIRE(not result.has_value());
  CHECK(result.error().op == FileOp::rename);
  CHECK(result.error().path == target);
  CHECK(static_cast<bool>(result.error().ec));
  CHECK(std::filesystem::is_directory(target));
  CHECK(read_bytes(target / "inside.txt") == "keep");
  CHECK(entry_names(dir.path()) == std::vector<std::string>{"occupied"});
}

TEST_CASE("a missing directory fails the create", "[io][atomic]") {
  const ScratchDir dir;
  const auto target = dir / "no_such_dir" / "out.txt";
  const auto result = mov::io::write_file_atomic(target, writing("x"));
  REQUIRE(not result.has_value());
  CHECK(result.error().op == FileOp::create);
  CHECK(result.error().path == target);
  CHECK(result.error().ec ==
        std::make_error_code(std::errc::no_such_file_or_directory));
  CHECK(entry_names(dir.path()).empty());
}

TEST_CASE("a directory that cannot be written fails the create",
          "[io][atomic][posix]") {
  const ScratchDir dir;
  const auto locked = dir / "locked";
  std::filesystem::create_directory(locked);
  std::filesystem::permissions(
      locked,
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
      std::filesystem::perm_options::replace);
  if (can_open_for_write(locked / "probe")) {
    SKIP("this process can write to read-only directories (running as root?)");
  }
  const auto result =
      mov::io::write_file_atomic(locked / "out.txt", writing("x"));
  REQUIRE(not result.has_value());
  CHECK(result.error().op == FileOp::create);
  CHECK(static_cast<bool>(result.error().ec));
}

TEST_CASE("TempFileGuard removes the file unless released", "[io][atomic]") {
  const ScratchDir dir;
  const auto doomed = dir / "doomed.tmp";
  const auto kept = dir / "kept.tmp";
  write_bytes(doomed, "x");
  write_bytes(kept, "x");
  {
    const mov::io::detail::TempFileGuard guard{doomed};
    CHECK(std::filesystem::exists(doomed));
  }
  CHECK(not std::filesystem::exists(doomed));
  {
    mov::io::detail::TempFileGuard guard{kept};
    guard.release();
  }
  CHECK(std::filesystem::exists(kept));
  // A file that is already gone is not an error.
  {
    const mov::io::detail::TempFileGuard guard{dir / "never_existed.tmp"};
  }
}

TEST_CASE("commit_temp makes a temporary file the target", "[io][atomic]") {
  const ScratchDir dir;
  const auto target = dir / "final.txt";
  const auto temp = dir / "final.txt.tmp";
  write_bytes(target, "old");
  write_bytes(temp, "new");
  REQUIRE(mov::io::detail::commit_temp(target, temp, {}).has_value());
  CHECK(read_bytes(target) == "new");
  CHECK(not std::filesystem::exists(temp));
}

TEST_CASE("commit_temp fails for a temporary file that is not there",
          "[io][atomic]") {
  const ScratchDir dir;
  const auto result =
      mov::io::detail::commit_temp(dir / "final.txt", dir / "missing.tmp", {});
  REQUIRE(not result.has_value());
  CHECK(result.error().op == FileOp::fsync);
  CHECK(static_cast<bool>(result.error().ec));
}
