// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <variant>

#include "mov/core/cancelled.hpp"
#include "mov/io/read_limits.hpp"

/// gzip decompression (docs/harmonics-engine.md section 6.5,
/// docs/providers-design.md sections 4.2 and 4.8): NDBC's `.txt.gz` files and
/// gzipped harmonics files. zlib is private to the implementation
/// (src/io/gzip.cpp is the only source that includes it).
namespace mov::io {

/// The two bytes every gzip member starts with (RFC 1952 section 2.3.1).
inline constexpr std::array<std::byte, 2> gzip_magic{std::byte{0x1F},
                                                     std::byte{0x8B}};

/// Whether `bytes` starts with the gzip magic. A body is inflated by what
/// it is, not by its URL or Content-Type: NDBC serves `.txt.gz` files as
/// application/x-gzip without Content-Encoding, while a body that Qt already
/// inflated (Content-Encoding) or a `.txt` file is text.
[[nodiscard]] constexpr bool has_gzip_magic(
    std::span<const std::byte> bytes) noexcept {
  return bytes.size() >= gzip_magic.size() and bytes[0] == gzip_magic[0] and
         bytes[1] == gzip_magic[1];
}

/// Why a gzip body does not inflate.
enum class GzipErrc : std::uint8_t {
  /// The input does not start with the gzip magic (the empty input too).
  not_gzip,
  /// zlib refused the stream: a bad header, invalid deflate data, or a CRC
  /// or length trailer that does not match the data.
  corrupt,
  /// The input ended before the member did.
  truncated,
  /// Another gzip member follows the first. Every NDBC file measured so far
  /// is one member (section 1.3); P8 counts more before this is relaxed.
  extra_member,
  /// Bytes after the member that are not a gzip member.
  trailing_bytes,
  /// The output would exceed the caller's cap (a decompression bomb, or
  /// just a file larger than allowed). No more than cap + 1 bytes were ever
  /// allocated for it.
  too_large,
};

/// A gzip failure and the offset into the compressed input where it was
/// found (for `too_large`, the input consumed when the cap was passed; for
/// `extra_member` and `trailing_bytes`, the first byte after the member).
struct GzipError {
  GzipErrc code;
  std::uint64_t offset;
  friend bool operator==(const GzipError&, const GzipError&) = default;
};

/// What gunzip can fail with: the stream, or the caller's stop request.
using GunzipError = std::variant<GzipError, core::Cancelled>;

/// The decompressed bytes of exactly one gzip member that fills `compressed`:
/// a second member or any trailing byte is an error, not ignored. At most
/// `max_out` bytes are returned; one more is `too_large`.
///
/// Memory: the output never holds more than `max_out` + 1 bytes, however
/// much the input claims. Its capacity may be larger, by the string's growth
/// policy (at most twice the size in libstdc++, libc++ and MSVC's STL), so
/// the allocation is below 2 * (`max_out` + 1). An exact bound would need a
/// buffer the caller cannot take as a std::string without a full copy,
/// which costs the same peak.
///
/// Each step feeds zlib at most 1 MiB of input and takes at most 1 MiB of
/// output (from 64 KiB, growing with the output), and `stop` is polled
/// before each step. Allocation failure (also inside zlib) throws
/// std::bad_alloc; a zlib that refuses its own initialization throws
/// std::logic_error.
[[nodiscard]] std::expected<std::string, GunzipError> gunzip(
    std::span<const std::byte> compressed, std::uintmax_t max_out,
    const StopToken& stop = {});

}  // namespace mov::io
