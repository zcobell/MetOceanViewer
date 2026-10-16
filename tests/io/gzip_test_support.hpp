// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// gzip bodies for the tests and fuzz targets, made with zlib itself (tests
// may include it; src may not, outside src/io/gzip.cpp), and bytes views of
// text. No Catch2: the fuzz targets include it too; a zlib failure throws.

#pragma once

#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#define ZLIB_CONST  // NOLINT(cppcoreguidelines-macro-usage)
#include <zlib.h>

namespace mov::test {

/// The bytes of `text` (a view: `text` must outlive it).
[[nodiscard]] inline std::span<const std::byte> bytes_of(
    std::string_view text) noexcept {
  return std::as_bytes(std::span{text.data(), text.size()});
}

/// Optional gzip header fields (RFC 1952 FNAME, FCOMMENT).
struct GzipHeader {
  std::string name{};
  std::string comment{};
};

/// One gzip member holding `plain`.
[[nodiscard]] inline std::string gzip_of(std::string_view plain,
                                         const GzipHeader& header = {}) {
  constexpr int gzip_window = 16 + MAX_WBITS;
  constexpr int memory_level = 8;
  z_stream z{};
  if (deflateInit2(&z, Z_BEST_COMPRESSION, Z_DEFLATED, gzip_window,
                   memory_level, Z_DEFAULT_STRATEGY) != Z_OK) {
    throw std::runtime_error{"deflateInit2"};
  }
  std::string name = header.name;
  std::string comment = header.comment;
  gz_header h{};
  if (not name.empty() or not comment.empty()) {
    h.name = name.empty()
                 ? nullptr
                 : static_cast<Bytef*>(static_cast<void*>(name.data()));
    h.comment = comment.empty()
                    ? nullptr
                    : static_cast<Bytef*>(static_cast<void*>(comment.data()));
    deflateSetHeader(&z, &h);
  }
  // Above deflateBound (n + n/4096 + n/16384 + 13 + the header) for any n.
  constexpr std::size_t header_bytes = 64;
  std::string out(plain.size() + plain.size() / 1024 + header_bytes +
                      name.size() + comment.size(),
                  '\0');
  z.next_in = static_cast<const Bytef*>(static_cast<const void*>(plain.data()));
  z.avail_in = static_cast<uInt>(plain.size());
  z.next_out = static_cast<Bytef*>(static_cast<void*>(out.data()));
  z.avail_out = static_cast<uInt>(out.size());
  const int status = deflate(&z, Z_FINISH);
  out.resize(z.total_out);
  deflateEnd(&z);
  if (status != Z_STREAM_END) {
    throw std::runtime_error{"deflate"};
  }
  return out;
}

}  // namespace mov::test
