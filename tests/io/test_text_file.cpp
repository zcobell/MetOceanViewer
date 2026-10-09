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

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

using namespace std::string_literals;
using mov::io::FileOp;
using mov::io::read_text_file;
using mov::io::ReadLimits;
using mov::test::fixture;

constexpr std::string_view bom_crlf_content =
    "\xEF\xBB\xBFline one\r\nline two\r\n\r\nlast line without newline";

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
  CHECK(result.error().op == FileOp::open);
  CHECK(result.error().path == missing);
  CHECK(result.error().ec == std::errc::no_such_file_or_directory);
  // Neither an empty string nor a created file.
  CHECK(not std::filesystem::exists(missing));
}

TEST_CASE("read_text_file reports an empty path", "[io][text_file]") {
  const auto result = read_text_file(std::filesystem::path{}, {});
  REQUIRE(not result.has_value());
  CHECK(result.error().op == FileOp::open);
  CHECK(result.error().ec == std::errc::no_such_file_or_directory);
}

TEST_CASE("read_text_file refuses a directory", "[io][text_file]") {
  const mov::test::ScratchDir dir;
  const auto result = read_text_file(dir.path(), {});
  REQUIRE(not result.has_value());
  CHECK(result.error().op == FileOp::open);
  CHECK(result.error().path == dir.path());
  CHECK(result.error().ec == std::errc::is_a_directory);
}

#ifndef _WIN32
TEST_CASE("read_text_file refuses a file that is not a regular file",
          "[io][text_file][posix]") {
  const auto result = read_text_file("/dev/null", {});
  REQUIRE(not result.has_value());
  CHECK(result.error().op == FileOp::open);
  CHECK(result.error().path == "/dev/null");
  CHECK(result.error().ec == std::errc::invalid_argument);
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
  CHECK(over.error().op == FileOp::size);
  CHECK(over.error().path == file);
  CHECK(over.error().ec == std::errc::file_too_large);

  const auto zero = read_text_file(file, ReadLimits{.max_text_bytes = 0});
  REQUIRE(not zero.has_value());
  CHECK(zero.error().ec == std::errc::file_too_large);

  // An empty file fits a zero limit.
  mov::test::write_bytes(dir / "empty.txt", "");
  CHECK(read_text_file(dir / "empty.txt", ReadLimits{.max_text_bytes = 0})
            .has_value());
}

TEST_CASE("read_file_prefix reads at most the bytes asked for",
          "[io][text_file]") {
  const mov::test::ScratchDir dir;
  const std::filesystem::path file = dir / "twenty.txt";
  mov::test::write_bytes(file, "0123456789abcdefghij");

  const auto head = mov::io::read_file_prefix(file, 5);
  REQUIRE(head.has_value());
  CHECK(head->bytes == "01234");
  CHECK_FALSE(head->whole_file);
  // A file shorter than the prefix, or exactly as long, is read whole; an
  // empty prefix of a file that has bytes is not the whole file.
  const auto all = mov::io::read_file_prefix(file, 100);
  REQUIRE(all.has_value());
  CHECK(all->bytes == "0123456789abcdefghij");
  CHECK(all->whole_file);
  const auto exact = mov::io::read_file_prefix(file, 20);
  REQUIRE(exact.has_value());
  CHECK(exact->whole_file);
  const auto none = mov::io::read_file_prefix(file, 0);
  REQUIRE(none.has_value());
  CHECK(none->bytes.empty());
  CHECK_FALSE(none->whole_file);
  // No size limit applies: the file may be far over max_text_bytes.
  mov::test::write_bytes(dir / "big.txt", std::string(100000, 'q'));
  const auto big = mov::io::read_file_prefix(dir / "big.txt", 16);
  REQUIRE(big.has_value());
  CHECK(big->bytes.size() == 16);
  CHECK_FALSE(big->whole_file);
}

TEST_CASE("read_file_prefix refuses what read_text_file refuses",
          "[io][text_file]") {
  const mov::test::ScratchDir dir;
  const auto missing = mov::io::read_file_prefix(dir / "nope", 10);
  REQUIRE(not missing.has_value());
  CHECK(missing.error().op == FileOp::open);
  const auto directory = mov::io::read_file_prefix(dir.path(), 10);
  REQUIRE(not directory.has_value());
  CHECK(directory.error().ec == std::errc::is_a_directory);
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
  CHECK(result.error().ec == std::errc::permission_denied);
}

#ifndef _WIN32
// A FIFO has no writer; reading it as text must neither hang nor succeed.
TEST_CASE("read_text_file does not block on a FIFO", "[io][text_file][posix]") {
  const mov::test::ScratchDir dir;
  const std::filesystem::path fifo = dir / "pipe";
  if (::mkfifo(fifo.c_str(), 0600) != 0) {
    SKIP("cannot create a FIFO here");
  }
  const auto result = read_text_file(fifo, {});
  REQUIRE(not result.has_value());
  CHECK(result.error().op == FileOp::open);
  CHECK(result.error().ec == std::errc::invalid_argument);
}
#endif
