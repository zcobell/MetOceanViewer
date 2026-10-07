// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The blocks a bulk read is cut into: runs of the first dimension, each of
// rows_per_block(slab, slab_elements) outer indices with every inner range
// whole, so each block is contiguous in the row-major result.

#include <algorithm>
#include <cstddef>
#include <expected>
#include <functional>
#include <span>
#include <vector>

#include "internal.hpp"
#include "mov/io/netcdf/file.hpp"

namespace mov::io::nc {

std::expected<void, Error> File::for_each_block(const Slab& slab,
                                                const StopToken& stop,
                                                const BlockReader& read) const {
  const auto cancelled = [] { return std::unexpected{Error{Cancelled{}}}; };
  if (slab.empty()) {  // a scalar: one element
    if (stop.stop_requested()) {
      return cancelled();
    }
    return read(Block{.start = {},
                      .count = {},
                      .outer = DimRange{.start = 0, .count = 1},
                      .offset = 0,
                      .size = 1});
  }
  detail::Hyperslab h = detail::unzip(slab);
  if (std::ranges::find(h.count, std::size_t{0}) != h.count.end()) {
    return {};  // nothing to read
  }
  // The caller checked that the slab's element count fits in size_t.
  std::size_t inner = 1;
  for (const std::size_t count : std::span{h.count}.subspan(1)) {
    inner *= count;
  }
  const std::size_t rows = rows_per_block(slab, limits_.slab_elements);
  const std::size_t first = h.start.front();
  const std::size_t outer_count = h.count.front();
  for (std::size_t done = 0; done < outer_count; done += h.count.front()) {
    if (stop.stop_requested()) {
      return cancelled();
    }
    h.start.front() = first + done;
    h.count.front() = std::min(rows, outer_count - done);
    const Block block{
        .start = h.start,
        .count = h.count,
        .outer = DimRange{.start = h.start.front(), .count = h.count.front()},
        .offset = done * inner,
        .size = h.count.front() * inner};
    if (auto ok = read(block); not ok) {
      return ok;
    }
  }
  return {};
}

}  // namespace mov::io::nc
