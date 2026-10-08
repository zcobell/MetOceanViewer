// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

namespace mov::io {

/// Upper bounds a reader enforces before it allocates (C12). Exceeding one
/// is an error (`too_large`), never a truncation.
///
/// The netCDF reads (nc::File) check, before allocating, both the element
/// count against `max_elements` and the bytes of the result against
/// `max_result_bytes` (read_blocks: the same for each block, since it never
/// holds more). Their peak memory, for n elements of the request:
///   read<T>          n * sizeof(T)
///   read_blocks<T>   one block: rows_per_block(slab, slab_elements) outer
///                    indices of the slab, of sizeof(T) each element
///   read_samples     n * sizeof(Sample) (16) + one block of the raw type
///   read_char_rows   rows * sizeof(std::string) + 2 * n (the bytes are read
///                    once, then copied into the rows)
///   read_strings     n * sizeof(std::string) + the strings' bytes, plus one
///                    block of the library's own strings while it is copied
/// (the result bytes counted are the first two terms of each line).
struct ReadLimits {
  /// Elements per call and per reader result (about 1 GiB of doubles).
  std::size_t max_elements = std::size_t{1} << 27;
  /// Bytes of one netCDF attribute.
  std::size_t max_att_bytes = std::size_t{1} << 20;
  /// Bytes of one text file, checked against the file size before reading.
  std::uintmax_t max_text_bytes = std::uintmax_t{1} << 30;
  /// Elements per bulk netCDF read: the cancellation granularity.
  std::size_t slab_elements = std::size_t{1} << 20;
  /// Bytes of one netCDF read's result (see above).
  std::size_t max_result_bytes = std::size_t{1} << 30;
};

/// A cancellation request a reader polls between slabs and records. It wraps
/// a predicate rather than `std::stop_token`, which Apple libc++ (Xcode 16)
/// does not ship, and so the provider/app layer can pass `QPromise::isCanceled`
/// directly. A default token never requests a stop.
class StopToken {
 public:
  StopToken() = default;
  explicit StopToken(std::function<bool()> requested)
      : requested_{std::move(requested)} {}

  [[nodiscard]] bool stop_requested() const {
    return requested_ and requested_();
  }

 private:
  std::function<bool()> requested_;
};

/// What every reader takes besides its input: the limits and a cancellation
/// request, checked between slabs and between records.
struct ReadContext {
  ReadLimits limits{};
  StopToken stop{};
};

}  // namespace mov::io
