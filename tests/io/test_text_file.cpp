// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "mov/io/detail/line_cursor.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/text_file.hpp"
#include "mov/test/fixture.hpp"
#include "mov/test/scratch_dir.hpp"

namespace {

using namespace std::string_literals;
using mov::io::FileError;
using mov::io::FileOp;
using mov::io::read_text_file;
using mov::io::ReadLimits;
using mov::test::fixture;

constexpr std::string_view bom_crlf_content =
    "\xEF\xBB\xBFline one\r\nline two\r\n\r\nlast line without newline";

std::error_code code(std::errc e) { return std::make_error_code(e); }

// True if this process can open `path` for writing despite the permissions
// (root, or a file system that ignores them).
bool can_open_for_write(const std::filesystem::path& path) {
  std::ofstream out{path, std::ios::app | std::ios::binary};
  return out.is_open();
}

}  // namespace

TEST_CASE("read_text_file returns the bytes of a file exactly",
          "[io][text_file]") {
  const auto bom = read_text_file(fixture("io/text_file/bom_crlf.txt"), {});
  REQUIRE(bom.has_value());
  CHECK(*bom == bom_crlf_content);  // BOM and CR bytes are kept

  const auto plain = read_text_file(fixture("io/text_file/plain.txt"), {});
  REQUIRE(plain.has_value());
  CHECK(*plain == "plain ascii\nsecond line\n");

  const auto nul = read_text_file(fixture("io/text_file/with_nul.txt"), {});
  REQUIRE(nul.has_value());
  CHECK(*nul == "a\0b\n"s);

  const auto empty = read_text_file(fixture("io/text_file/empty.txt"), {});
  REQUIRE(empty.has_value());
  CHECK(empty->empty());
}

TEST_CASE("the byte order mark is removed once, by LineCursor",
          "[io][text_file]") {
  const auto content = read_text_file(fixture("io/text_file/bom_crlf.txt"), {});
  REQUIRE(content.has_value());
  mov::io::detail::LineCursor cursor{*content};
  const auto next_text = [&cursor]() {
    const auto line = cursor.next();
    return line ? std::optional<std::string>{std::string{line->text}}
                : std::nullopt;
  };
  CHECK(next_text() == "line one");
  CHECK(next_text() == "line two");
  CHECK(next_text() == "");
  CHECK(next_text() == "last line without newline");
  CHECK(next_text() == std::nullopt);
}

TEST_CASE("read_text_file reports a missing file", "[io][text_file]") {
  const mov::test::ScratchDir dir;
  const std::filesystem::path missing = dir / "missing.txt";
  const auto result = read_text_file(missing, {});
  REQUIRE(not result.has_value());
  CHECK(result.error() ==
        FileError{.op = FileOp::open,
                  .path = missing,
                  .ec = code(std::errc::no_such_file_or_directory)});
  // Neither an empty string nor a created file.
  CHECK(not std::filesystem::exists(missing));
}

TEST_CASE("read_text_file reports an empty path", "[io][text_file]") {
  const auto result = read_text_file(std::filesystem::path{}, {});
  REQUIRE(not result.has_value());
  CHECK(result.error().op == FileOp::open);
  CHECK(static_cast<bool>(result.error().ec));
}

TEST_CASE("read_text_file refuses a directory", "[io][text_file]") {
  const mov::test::ScratchDir dir;
  const auto result = read_text_file(dir.path(), {});
  REQUIRE(not result.has_value());
  CHECK(result.error() == FileError{.op = FileOp::open,
                                    .path = dir.path(),
                                    .ec = code(std::errc::is_a_directory)});
}

#ifndef _WIN32
TEST_CASE("read_text_file refuses a file that is not a regular file",
          "[io][text_file][posix]") {
  const auto result = read_text_file("/dev/null", {});
  REQUIRE(not result.has_value());
  CHECK(result.error() == FileError{.op = FileOp::open,
                                    .path = "/dev/null",
                                    .ec = code(std::errc::invalid_argument)});
}
#endif

TEST_CASE("read_text_file checks the size limit before reading",
          "[io][text_file]") {
  const mov::test::ScratchDir dir;
  const std::filesystem::path file = dir / "twenty.txt";
  mov::test::write_bytes(file, std::string(20, 'x'));

  const auto at_limit = read_text_file(file, ReadLimits{.max_text_bytes = 20});
  REQUIRE(at_limit.has_value());
  CHECK(at_limit->size() == 20);

  const auto over = read_text_file(file, ReadLimits{.max_text_bytes = 19});
  REQUIRE(not over.has_value());
  CHECK(over.error() == FileError{.op = FileOp::size,
                                  .path = file,
                                  .ec = code(std::errc::file_too_large)});

  const auto zero = read_text_file(file, ReadLimits{.max_text_bytes = 0});
  REQUIRE(not zero.has_value());
  CHECK(zero.error().ec == code(std::errc::file_too_large));

  // An empty file fits a zero limit.
  mov::test::write_bytes(dir / "empty.txt", "");
  CHECK(read_text_file(dir / "empty.txt", ReadLimits{.max_text_bytes = 0})
            .has_value());
}

// B3: v4 opened the file with std::fstream (read and write), so a read-only
// file failed to open and the reader returned "no stations" as a success.
TEST_CASE("read_text_file reads a read-only file",
          "[io][text_file][regression][B3]") {
  const mov::test::ScratchDir dir;
  const std::filesystem::path copy = dir / "readonly.txt";
  std::filesystem::copy_file(fixture("io/text_file/plain.txt"), copy);
  std::filesystem::permissions(copy,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::group_read |
                                   std::filesystem::perms::others_read,
                               std::filesystem::perm_options::replace);

  // The precondition: opening for write fails. As root it does not, and the
  // test would prove nothing.
  if (can_open_for_write(copy)) {
    SKIP("this process can write read-only files (running as root?)");
  }
  const auto result = read_text_file(copy, {});
  REQUIRE(result.has_value());
  CHECK(*result == "plain ascii\nsecond line\n");
}

TEST_CASE("read_text_file reports a file it may not read",
          "[io][text_file][posix]") {
  const mov::test::ScratchDir dir;
  const std::filesystem::path file = dir / "secret.txt";
  mov::test::write_bytes(file, "data");
  std::filesystem::permissions(file, std::filesystem::perms::none,
                               std::filesystem::perm_options::replace);
  if (std::ifstream{file}.is_open()) {
    SKIP("this process can read any file (running as root?)");
  }
  const auto result = read_text_file(file, {});
  REQUIRE(not result.has_value());
  CHECK(result.error().op == FileOp::open);
  CHECK(result.error().path == file);
  CHECK(static_cast<bool>(result.error().ec));
}
