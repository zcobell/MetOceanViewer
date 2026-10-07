// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <cstdint>
#include <stop_token>

namespace mov::io {

/// Upper bounds a reader enforces before it allocates (C12). Exceeding one
/// is an error (`too_large`), never a truncation.
struct ReadLimits {
  /// Elements per call and per reader result (about 1 GiB of doubles).
  std::size_t max_elements = std::size_t{1} << 27;
  /// Bytes of one netCDF attribute.
  std::size_t max_att_bytes = std::size_t{1} << 20;
  /// Bytes of one text file, checked against the file size before reading.
  std::uintmax_t max_text_bytes = std::uintmax_t{1} << 30;
  /// Elements per bulk netCDF read: the cancellation granularity.
  std::size_t slab_elements = std::size_t{1} << 20;
};

/// What every reader takes besides its input: the limits and a cancellation
/// request, checked between slabs and between records.
struct ReadContext {
  ReadLimits limits{};
  std::stop_token stop{};
};

}  // namespace mov::io
