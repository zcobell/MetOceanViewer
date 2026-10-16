// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// gunzip: the only source that includes zlib (the third_party_include_gate
// test checks it).

#include "mov/io/gzip.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string>

#include "mov/core/cancelled.hpp"
#include "mov/io/read_limits.hpp"

// next_in as a pointer to const: the input is never written.
#define ZLIB_CONST  // NOLINT(cppcoreguidelines-macro-usage)
#include <zlib.h>

namespace mov::io {

namespace {

// The output grows by what it holds already, from 64 KiB to 1 MiB a step: a
// small file costs no megabyte, a large one few steps.
constexpr std::size_t min_chunk = std::size_t{64} << 10;
constexpr std::size_t max_chunk = std::size_t{1} << 20;

// The input is handed to zlib 1 MiB at a time (zlib counts in uInt, and the
// stop request is polled between steps, so a step's work stays bounded).
constexpr std::size_t max_feed = std::size_t{1} << 20;
static_assert(max_feed <= std::numeric_limits<uInt>::max());

[[nodiscard]] const Bytef* zlib_in(const std::byte* p) noexcept {
  return static_cast<const Bytef*>(static_cast<const void*>(p));
}

[[nodiscard]] Bytef* zlib_out(char* p) noexcept {
  return static_cast<Bytef*>(static_cast<void*>(p));
}

// One gzip member's inflate state (windowBits 16 + MAX_WBITS: a gzip header
// and trailer, never zlib's or raw deflate), released on every path.
class Inflater {
 public:
  explicit Inflater(std::span<const std::byte> input) : input_{input} {
    const int status = inflateInit2(&stream_, 16 + MAX_WBITS);
    if (status == Z_MEM_ERROR) {
      throw std::bad_alloc{};
    }
    if (status != Z_OK) {
      // Z_VERSION_ERROR or Z_STREAM_ERROR: a zlib that does not match its
      // header, or arguments it refuses. Neither is the input's fault.
      throw std::logic_error{"inflateInit2 failed"};
    }
  }
  Inflater(const Inflater&) = delete;
  Inflater& operator=(const Inflater&) = delete;
  Inflater(Inflater&&) = delete;
  Inflater& operator=(Inflater&&) = delete;
  ~Inflater() { inflateEnd(&stream_); }

  // The bytes of the input zlib has taken.
  [[nodiscard]] std::uint64_t offset() const noexcept {
    return fed_ - stream_.avail_in;
  }

  [[nodiscard]] bool input_left() const noexcept {
    return stream_.avail_in > 0 or fed_ < input_.size();
  }

  struct Step {
    int status;
    bool filled;  // all of `room` was written: more output may be pending
  };

  // Inflates at most `room` more bytes onto the end of `out`, feeding the
  // next piece of input when zlib has none, and shrinks `out` to what was
  // written.
  Step step(std::string& out, std::size_t room) {
    if (stream_.avail_in == 0 and fed_ < input_.size()) {
      const std::size_t n = std::min(input_.size() - fed_, max_feed);
      stream_.next_in = zlib_in(input_.data() + fed_);
      stream_.avail_in = static_cast<uInt>(n);
      fed_ += n;
    }
    const std::size_t old = out.size();
    out.resize(old + room);
    stream_.next_out = zlib_out(out.data() + old);
    stream_.avail_out = static_cast<uInt>(room);
    const int status = inflate(&stream_, Z_NO_FLUSH);
    const bool filled = stream_.avail_out == 0;
    out.resize(old + room - stream_.avail_out);
    return {.status = status, .filled = filled};
  }

 private:
  std::span<const std::byte> input_;
  std::size_t fed_ = 0;
  z_stream stream_{};
};

[[nodiscard]] std::unexpected<GunzipError> failure(GzipErrc code,
                                                   std::uint64_t offset) {
  return std::unexpected{
      GunzipError{GzipError{.code = code, .offset = offset}}};
}

}  // namespace

std::expected<std::string, GunzipError> gunzip(
    std::span<const std::byte> compressed, std::uintmax_t max_out,
    const StopToken& stop) {
  if (not has_gzip_magic(compressed)) {
    return failure(GzipErrc::not_gzip, 0);
  }
  // The output may reach max_out + 1 bytes: the one that proves too_large.
  const std::uintmax_t limit =
      max_out == std::numeric_limits<std::uintmax_t>::max() ? max_out
                                                            : max_out + 1;
  Inflater z{compressed};
  std::string out;
  int status = Z_OK;
  while (status != Z_STREAM_END) {
    if (stop.stop_requested()) {
      return std::unexpected{GunzipError{core::Cancelled{}}};
    }
    const std::size_t chunk = std::clamp(out.size(), min_chunk, max_chunk);
    const auto room = static_cast<std::size_t>(
        std::min<std::uintmax_t>(chunk, limit - out.size()));
    const Inflater::Step step = z.step(out, room);
    status = step.status;
    if (out.size() > max_out) {
      return failure(GzipErrc::too_large, z.offset());
    }
    if (status == Z_MEM_ERROR) {
      throw std::bad_alloc{};
    }
    if (status == Z_STREAM_ERROR) {
      throw std::logic_error{"inflate: inconsistent stream state"};
    }
    if (status == Z_DATA_ERROR or status == Z_NEED_DICT) {
      return failure(GzipErrc::corrupt, z.offset());
    }
    if (status != Z_STREAM_END and not step.filled and not z.input_left()) {
      return failure(GzipErrc::truncated, z.offset());
    }
  }
  const std::uint64_t end = z.offset();
  if (end < compressed.size()) {
    const auto rest = compressed.subspan(static_cast<std::size_t>(end));
    return failure(has_gzip_magic(rest) ? GzipErrc::extra_member
                                        : GzipErrc::trailing_bytes,
                   end);
  }
  return out;
}

}  // namespace mov::io
