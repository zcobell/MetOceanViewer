// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The blocks a bulk read is split into (internal.hpp, for_each_block).

#include <algorithm>
#include <cstddef>
#include <expected>
#include <span>
#include <vector>

#include "internal.hpp"

namespace mov::io::nc::detail {

namespace {

/// The dimension the blocks are cut along: the outermost one whose inner
/// dimensions together hold at most `max_block` elements.
struct Split {
  std::size_t dim;
  std::size_t inner;   // elements per index of `dim`
  std::size_t length;  // indices of `dim` per block
};

[[nodiscard]] Split split_for(std::span<const std::size_t> count,
                              std::size_t max_block) {
  std::size_t inner = 1;
  std::size_t dim = count.size() - 1;
  // Walk outwards while the next dimension still fits in one block.
  while (dim > 0 and inner * count[dim] <= max_block) {
    inner *= count[dim];
    --dim;
  }
  return Split{
      .dim = dim,
      .inner = inner,
      .length = std::clamp<std::size_t>(max_block / inner, 1, count[dim])};
}

/// Moves `pos` to the next block (an odometer over the dimensions outside
/// the split one). False when every block has been visited.
[[nodiscard]] bool advance(std::span<std::size_t> pos,
                           std::span<const std::size_t> start,
                           std::span<const std::size_t> count,
                           const Split& split, std::size_t step) {
  std::size_t dim = split.dim;
  pos[dim] += step;
  while (pos[dim] == start[dim] + count[dim]) {
    if (dim == 0) {
      return false;
    }
    pos[dim] = start[dim];
    --dim;
    ++pos[dim];
  }
  return true;
}

}  // namespace

std::expected<void, Error> for_each_block(const Slab& slab,
                                          std::size_t max_block,
                                          const StopToken& stop,
                                          const BlockVisitor& visit) {
  std::vector<std::size_t> start;
  std::vector<std::size_t> count;
  for (const DimRange& range : slab) {
    start.push_back(range.start);
    count.push_back(range.count);
  }
  if (std::ranges::find(count, std::size_t{0}) != count.end()) {
    return {};
  }
  if (stop.stop_requested()) {
    return std::unexpected{Error{Cancelled{}}};
  }
  if (slab.empty()) {  // a scalar: one element
    return visit(Block{.start = {}, .count = {}, .offset = 0, .size = 1});
  }
  const Split split = split_for(count, std::max<std::size_t>(max_block, 1));
  std::vector<std::size_t> pos = start;
  std::vector<std::size_t> shape = count;
  std::fill_n(shape.begin(), split.dim, std::size_t{1});
  std::size_t offset = 0;
  while (true) {
    shape[split.dim] = std::min(
        split.length, start[split.dim] + count[split.dim] - pos[split.dim]);
    const std::size_t size = shape[split.dim] * split.inner;
    if (auto done = visit(Block{
            .start = pos, .count = shape, .offset = offset, .size = size});
        not done) {
      return done;
    }
    offset += size;
    if (not advance(pos, start, count, split, shape[split.dim])) {
      return {};
    }
    if (stop.stop_requested()) {
      return std::unexpected{Error{Cancelled{}}};
    }
  }
}

}  // namespace mov::io::nc::detail
