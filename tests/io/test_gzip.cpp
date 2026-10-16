// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// io::gunzip (docs/harmonics-engine.md section 6.5, docs/providers-design.md
// section 4.2): the real NDBC file, round trips, and every way a body can be
// refused. The inputs are made with zlib itself (tests may include it).

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <variant>

#include "gzip_test_support.hpp"
#include "mov/core/cancelled.hpp"
#include "mov/core/stop_token.hpp"
#include "mov/io/gzip.hpp"
#include "mov/test/fixture.hpp"
#include "mov/test/scratch_dir.hpp"

namespace {

using mov::io::gunzip;
using mov::io::GzipErrc;
using mov::io::GzipError;
using mov::test::bytes_of;
using mov::test::gzip_of;

constexpr std::uintmax_t no_cap = std::numeric_limits<std::uintmax_t>::max();

GzipError gzip_error_in(
    const std::expected<std::string, mov::io::GunzipError>& r) {
  REQUIRE(not r.has_value());
  const auto* e = std::get_if<GzipError>(&r.error());
  REQUIRE(e != nullptr);
  return *e;
}

// The ISIZE trailer of a gzip member: its uncompressed size modulo 2^32.
std::uint32_t isize_of(std::string_view gz) {
  REQUIRE(gz.size() >= 4);
  std::uint32_t n = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    n |= static_cast<std::uint32_t>(
             static_cast<unsigned char>(gz[gz.size() - 4 + i]))
         << (8U * i);
  }
  return n;
}

}  // namespace

TEST_CASE("the gzip magic decides, nothing else", "[io][gzip]") {
  CHECK(mov::io::has_gzip_magic(bytes_of("\x1f\x8b")));
  CHECK(mov::io::has_gzip_magic(bytes_of("\x1f\x8b\x08 and more")));
  CHECK_FALSE(mov::io::has_gzip_magic(bytes_of("")));
  CHECK_FALSE(mov::io::has_gzip_magic(bytes_of("\x1f")));
  CHECK_FALSE(mov::io::has_gzip_magic(bytes_of("\x8b\x1f")));
  CHECK_FALSE(mov::io::has_gzip_magic(bytes_of("#YY  MM DD hh mm WDIR")));
}

TEST_CASE("the recorded NDBC file inflates to its text", "[io][gzip][ndbc]") {
  const std::string gz =
      mov::test::read_bytes(mov::test::fixture("io/ndbc/41002h1973.txt.gz"));
  const auto text = gunzip(bytes_of(gz), no_cap);
  REQUIRE(text.has_value());
  CHECK(text->size() == isize_of(gz));
  CHECK(text->starts_with("YY MM DD hh WD   WSPD GST"));
  CHECK(text->ends_with("\n"));
}

TEST_CASE("round trips, empty to several chunks", "[io][gzip]") {
  for (const std::size_t n : {std::size_t{0}, std::size_t{1}, std::size_t{4096},
                              (std::size_t{3} << 20) + 17}) {
    std::string plain(n, '\0');
    for (std::size_t i = 0; i < n; ++i) {
      plain[i] = static_cast<char>('a' + (i * 7 + i / 13) % 26);
    }
    const auto out = gunzip(bytes_of(gzip_of(plain)), no_cap);
    REQUIRE(out.has_value());
    CHECK(*out == plain);
  }
}

TEST_CASE("a header with a file name and comment is read past", "[io][gzip]") {
  const std::string gz =
      gzip_of("data\n", {.name = "41002h1973.txt", .comment = "c"});
  const auto out = gunzip(bytes_of(gz), no_cap);
  REQUIRE(out.has_value());
  CHECK(*out == "data\n");
}

TEST_CASE("text is not gzip", "[io][gzip]") {
  for (const std::string_view body : std::array<std::string_view, 4>{
           "", "\x1f", "#YY MM DD", "Unable to access data"}) {
    const GzipError e = gzip_error_in(gunzip(bytes_of(body), no_cap));
    CHECK(e.code == GzipErrc::not_gzip);
    CHECK(e.offset == 0);
  }
}

TEST_CASE("a truncated member", "[io][gzip]") {
  const std::string gz = gzip_of(std::string(10000, 'x') + "tail");
  for (const std::size_t keep : {std::size_t{2}, std::size_t{10}, gz.size() / 2,
                                 gz.size() - 4, gz.size() - 1}) {
    INFO("kept " << keep << " of " << gz.size());
    const GzipError e = gzip_error_in(
        gunzip(bytes_of(std::string_view{gz}.substr(0, keep)), no_cap));
    CHECK(e.code == GzipErrc::truncated);
    CHECK(e.offset <= keep);
  }
}

TEST_CASE("a bad CRC, a bad length, bad deflate data", "[io][gzip]") {
  const std::string good = gzip_of("the quick brown fox\n");
  std::string bad_crc = good;
  bad_crc[bad_crc.size() - 8] =
      static_cast<char>(bad_crc[bad_crc.size() - 8] ^ 0x01);
  std::string bad_length = good;
  bad_length.back() = static_cast<char>(bad_length.back() ^ 0x01);
  std::string bad_method = good;
  bad_method[2] = 0x07;  // CM must be 8 (deflate)
  for (const std::string& gz : {bad_crc, bad_length, bad_method}) {
    CHECK(gzip_error_in(gunzip(bytes_of(gz), no_cap)).code ==
          GzipErrc::corrupt);
  }
}

TEST_CASE("one member only: a second one is refused where it starts",
          "[io][gzip]") {
  const std::string first = gzip_of("first\n");
  const std::string two = first + gzip_of("second\n");
  const GzipError e = gzip_error_in(gunzip(bytes_of(two), no_cap));
  CHECK(e.code == GzipErrc::extra_member);
  CHECK(e.offset == first.size());
}

TEST_CASE("bytes after the member are refused, zeros included", "[io][gzip]") {
  const std::string member = gzip_of("data\n");
  for (const std::string_view tail :
       {std::string_view{"x"}, std::string_view{"\0\0\0\0", 4},
        std::string_view{"\x1f"}, std::string_view{"\n"}}) {
    const GzipError e =
        gzip_error_in(gunzip(bytes_of(member + std::string{tail}), no_cap));
    CHECK(e.code == GzipErrc::trailing_bytes);
    CHECK(e.offset == member.size());
  }
}

TEST_CASE("the output cap: exact is allowed, one byte more is too large",
          "[io][gzip]") {
  const std::string plain(5000, 'z');
  const std::string gz = gzip_of(plain);
  CHECK(gunzip(bytes_of(gz), plain.size()).value() == plain);
  CHECK(gzip_error_in(gunzip(bytes_of(gz), plain.size() - 1)).code ==
        GzipErrc::too_large);
  CHECK(gunzip(bytes_of(gzip_of("")), 0).value().empty());
  CHECK(gzip_error_in(gunzip(bytes_of(gzip_of("a")), 0)).code ==
        GzipErrc::too_large);
}

TEST_CASE("a decompression bomb stops at the cap", "[io][gzip]") {
  // 64 MiB of zeros deflate to about 64 KiB.
  const std::string gz = gzip_of(std::string(std::size_t{64} << 20, '\0'));
  REQUIRE(gz.size() < (std::size_t{1} << 20));
  const GzipError e = gzip_error_in(gunzip(bytes_of(gz), std::size_t{1} << 20));
  CHECK(e.code == GzipErrc::too_large);
  CHECK(e.offset < gz.size());
}

TEST_CASE("a stop request cancels before the next chunk", "[io][gzip]") {
  const std::string gz = gzip_of(std::string(std::size_t{3} << 20, 'q'));
  const mov::core::StopToken stop{[] { return true; }};
  const auto r = gunzip(bytes_of(gz), no_cap, stop);
  REQUIRE(not r.has_value());
  CHECK(std::holds_alternative<mov::core::Cancelled>(r.error()));
}

TEST_CASE("input is fed 1 MiB a step, so the stop is polled through it",
          "[io][gzip]") {
  // Incompressible bytes: the member is larger than its 4 MiB of output, so
  // the steps are paced by the input, at most 1 MiB each.
  std::string noise(std::size_t{4} << 20, '\0');
  std::uint64_t state = 0x9E3779B97F4A7C15ULL;
  for (char& c : noise) {
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    c = static_cast<char>(state & 0xFFU);
  }
  const std::string gz = gzip_of(noise);
  REQUIRE(gz.size() > noise.size());
  std::size_t polls = 0;
  const mov::core::StopToken counting{[&polls] {
    ++polls;
    return false;
  }};
  const auto out = gunzip(bytes_of(gz), no_cap, counting);
  CHECK(out == noise);
  CHECK(polls >= gz.size() >> 20U);
  // Stopped at the third poll: cancelled with the input mostly unread.
  std::size_t asked = 0;
  const mov::core::StopToken third{[&asked] { return ++asked == 3; }};
  const auto stopped = gunzip(bytes_of(gz), no_cap, third);
  REQUIRE(not stopped.has_value());
  CHECK(std::holds_alternative<mov::core::Cancelled>(stopped.error()));
  CHECK(asked == 3);
}
