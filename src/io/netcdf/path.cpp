// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// How a path is handed to nc_open and nc_create.
//
// POSIX: the path's bytes, unchanged (a file name need not be UTF-8).
//
// Windows: netCDF-C 4.9.3 takes a `const char*` in the *active code page*
// (ACP), not UTF-8: libdispatch/dpathmgr.c (ansi2utf8, NCfopen, NCopen3) and
// libhdf5/hdf5open.c / hdf5create.c (nc4_H5Fopen, nc4_H5Fcreate call
// NCpath2utf8 = ansi2utf8) convert it from the ACP to UTF-8 for HDF5, unless
// the process runs with the UTF-8 code page (an application manifest's
// activeCodePage), in which case the bytes are taken as UTF-8. So the wide
// path is converted to the ACP; when the ACP cannot represent it, the 8.3
// short name (pure ASCII) is used instead, of the file or, for a file not yet
// created, of its directory. With neither, the path is
// `unrepresentable_path`. NOT VERIFIED on Windows: worked out from the
// netCDF-C source above, not yet run there.

#include <expected>
#include <filesystem>
#include <string>

#include "internal.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <limits>
#include <optional>
#include <vector>
#endif

namespace mov::io::nc::detail {

#if defined(_WIN32)

namespace {

// `wide` in code page `cp`, or nullopt when a character has no exact
// representation there (no best-fit substitutes: "é" must not become "e").
std::optional<std::string> to_code_page(const std::wstring& wide, UINT cp) {
  if (wide.empty()) {
    return std::string{};
  }
  if (wide.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return std::nullopt;
  }
  const int length = static_cast<int>(wide.size());
  const bool utf8 = cp == CP_UTF8;
  const DWORD flags = utf8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
  BOOL lossy = FALSE;
  BOOL* const lossy_out = utf8 ? nullptr : &lossy;
  const int bytes = ::WideCharToMultiByte(cp, flags, wide.data(), length,
                                          nullptr, 0, nullptr, lossy_out);
  if (bytes <= 0 or lossy != FALSE) {
    return std::nullopt;
  }
  std::string out(static_cast<std::size_t>(bytes), '\0');
  if (::WideCharToMultiByte(cp, flags, wide.data(), length, out.data(), bytes,
                            nullptr, lossy_out) != bytes or
      lossy != FALSE) {
    return std::nullopt;
  }
  return out;
}

// The 8.3 short form of an existing path, or nullopt.
std::optional<std::wstring> short_path(const std::filesystem::path& path) {
  const std::wstring& wide = path.native();
  const DWORD needed = ::GetShortPathNameW(wide.c_str(), nullptr, 0);
  if (needed == 0) {
    return std::nullopt;
  }
  std::vector<wchar_t> buffer(needed);
  const DWORD written =
      ::GetShortPathNameW(wide.c_str(), buffer.data(), needed);
  if (written == 0 or written >= needed) {
    return std::nullopt;
  }
  return std::wstring{buffer.data(), written};
}

}  // namespace

std::expected<std::string, WrapperFault> nc_path(
    const std::filesystem::path& path) {
  const UINT cp = ::GetACP();
  if (auto direct = to_code_page(path.native(), cp)) {
    return *std::move(direct);
  }
  std::optional<std::wstring> shortened = short_path(path);
  if (not shortened and path.has_parent_path()) {
    shortened = short_path(path.parent_path()).transform([&](std::wstring dir) {
      return (std::filesystem::path{dir} / path.filename()).native();
    });
  }
  if (auto converted = shortened.and_then(
          [cp](const std::wstring& s) { return to_code_page(s, cp); })) {
    return *std::move(converted);
  }
  return std::unexpected{WrapperFault::unrepresentable_path};
}

#else

std::expected<std::string, WrapperFault> nc_path(
    const std::filesystem::path& path) {
  return path.native();
}

#endif

}  // namespace mov::io::nc::detail
