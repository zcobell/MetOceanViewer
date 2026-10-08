// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Helpers shared by the translation units of the netCDF wrapper. Private to
// src/io/netcdf/, and free of <netcdf.h>: an nc_type is an int here.

#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>

#include "mov/io/error.hpp"
#include "mov/io/netcdf/name.hpp"
#include "mov/io/netcdf/types.hpp"
#include "mov/io/read_limits.hpp"

namespace mov::io::nc::detail {

/// The wrapper's name of a netCDF-C type id.
[[nodiscard]] Type to_type(int xtype) noexcept;

/// The netCDF-C type id of `type` (Type::other has none: NC_NAT).
[[nodiscard]] int to_nc_type(Type type) noexcept;

/// `var:att`, or `:att` for a global attribute (ncdump's notation): the one
/// formatter of attribute names in errors.
[[nodiscard]] std::string att_object(const AttTarget& on, NcNameRef att);

/// The path as netCDF-C's nc_open and nc_create expect it: the native bytes
/// on POSIX; on Windows, text in the active code page (netCDF-C 4.9.3 reads
/// it so: libdispatch/dpathmgr.c, ansi2utf8), with an 8.3 short name as the
/// fallback for a path that code page cannot represent.
[[nodiscard]] std::expected<std::string, WrapperFault> nc_path(
    const std::filesystem::path& path);

/// The element count of `slab` of `var` after checking it: the rank
/// (`rank_mismatch`), each range inside its dimension (NC_EINVALCOORDS,
/// NC_EEDGE), the product (`overflow`) and, when `whole_result` (the read
/// holds all of it), limits.max_elements and, for results of `element_bytes`
/// each, limits.max_result_bytes (`too_large`). A read that holds one block at
/// a time (read_blocks) checks each block instead.
[[nodiscard]] std::expected<std::size_t, NcStatus> check_slab(
    const VarInfo& var, const Slab& slab, const ReadLimits& limits,
    std::size_t element_bytes, bool whole_result = true);

/// Whether `count` elements of `element_bytes` each, plus `extra_bytes`,
/// fit in `limit` bytes (no overflow).
[[nodiscard]] constexpr bool fits_bytes(std::size_t count,
                                        std::size_t element_bytes,
                                        std::size_t extra_bytes,
                                        std::size_t limit) noexcept {
  return extra_bytes <= limit and
         (element_bytes == 0 or count <= (limit - extra_bytes) / element_bytes);
}

}  // namespace mov::io::nc::detail
