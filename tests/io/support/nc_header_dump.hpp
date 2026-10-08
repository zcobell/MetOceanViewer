// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// `ncdump -h` without ncdump: the header of a netCDF file as CDL, printed the
// way netCDF-C's ncdump 4.9 prints it, with the raw netCDF-C API (independent
// of mov::io). The golden CDL tests compare it with committed CDL, and the
// format-compliance job compares the real `ncdump -h` with the same files,
// so the two cannot drift apart unnoticed.
//
// Covered: root-group dimensions (fixed and unlimited), variables of the
// atomic types, and their attributes of the atomic types (NC_STRING
// attributes are printed with ncdump's `string` prefix). Not covered: groups
// and user-defined types (the station format has neither).

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace mov::test::ncgen {

/// The CDL header of `path`, as `ncdump -h` prints it, named `name` (ncdump
/// uses the file name without its extension). Throws std::runtime_error on
/// any netCDF-C error.
[[nodiscard]] std::string ncdump_header(const std::filesystem::path& path,
                                        std::string_view name);

/// A small file with a dimension of each kind, a variable of every atomic
/// type and attributes of every atomic type, with the values whose printing
/// differs most (fill values, NaN, escapes, a newline in a text): the probe
/// the format-compliance job prints with the real ncdump and the golden test
/// with ncdump_header, against the same committed CDL.
void make_ncdump_probe(const std::filesystem::path& path);

/// `cdl` with every "MetOceanViewer <major>.<minor>.<patch>" replaced by
/// "MetOceanViewer X.Y.Z": the application version in `history` is the only
/// part of a canonical header that changes between releases.
[[nodiscard]] std::string without_app_version(std::string_view cdl);

}  // namespace mov::test::ncgen
