// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Helpers shared by the translation units of the netCDF wrapper. Private to
// src/io/netcdf/, and free of <netcdf.h>: an nc_type is an int here.

#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "mov/io/error.hpp"
#include "mov/io/netcdf/name.hpp"
#include "mov/io/netcdf/types.hpp"
#include "mov/io/read_limits.hpp"

namespace mov::io::nc::detail {

/// The wrapper's name of a netCDF-C type id.
[[nodiscard]] Type to_type(int xtype) noexcept;

/// The netCDF-C type id of `type` (Type::other has none: NC_NAT).
[[nodiscard]] int to_nc_type(Type type) noexcept;

/// `var:att`, or `:att` for a global attribute (ncdump's notation).
[[nodiscard]] std::string att_object(const AttTarget& on, NcNameRef att);

/// The path as netCDF-C's nc_open and nc_create expect it: the native bytes
/// on POSIX; on Windows, text in the active code page (netCDF-C 4.9.3 reads
/// it so: libdispatch/dpathmgr.c, ansi2utf8), with an 8.3 short name as the
/// fallback for a path that code page cannot represent.
[[nodiscard]] std::expected<std::string, WrapperFault> nc_path(
    const std::filesystem::path& path);

/// The element count of `slab` of `var` after checking it: the rank
/// (`rank_mismatch`), each range inside its dimension (NC_EINVALCOORDS,
/// NC_EEDGE), the product (`overflow`) and limits.max_elements
/// (`too_large`).
[[nodiscard]] std::expected<std::size_t, NcStatus> check_slab(
    const VarInfo& var, const Slab& slab, const ReadLimits& limits);

/// One block of a slab: its corner, its shape, and where its first element
/// goes in the row-major result.
struct Block {
  std::span<const std::size_t> start;
  std::span<const std::size_t> count;
  std::size_t offset;
  std::size_t size;
};

/// Calls `visit(Block)` for consecutive row-major blocks that cover `slab`
/// once, each of at most max(1, max_block) elements and contiguous in the
/// result, and polls `stop` before each (Cancelled). A slab with a zero count
/// has no blocks. Precondition: the slab's element count fits in size_t
/// (check_slab).
using BlockVisitor = std::function<std::expected<void, Error>(const Block&)>;
[[nodiscard]] std::expected<void, Error> for_each_block(
    const Slab& slab, std::size_t max_block, const StopToken& stop,
    const BlockVisitor& visit);

}  // namespace mov::io::nc::detail
