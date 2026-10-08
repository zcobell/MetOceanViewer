// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/file_type.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/core/detail/ascii.hpp"
#include "mov/core/units.hpp"
#include "mov/io/adcirc_ascii.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/error.hpp"
#include "mov/io/hwm_file.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/text_file.hpp"
#include "netcdf_kind.hpp"

namespace mov::io {

namespace {

/// Enough for the header of any text format and many rows of it.
constexpr std::size_t sniff_bytes = std::size_t{1} << 16U;
/// The lines looked at.
constexpr std::size_t sniff_lines = 64;
constexpr std::size_t hwm_sniff_rows = 16;

constexpr std::array<std::string_view, 3> classic_magic{
    std::string_view{"CDF\x01", 4}, std::string_view{"CDF\x02", 4},
    std::string_view{"CDF\x05", 4}};
constexpr std::string_view hdf5_magic{"\x89HDF\r\n\x1a\n", 8};

bool is_netcdf_magic(std::string_view bytes) {
  return bytes.starts_with(hdf5_magic) or
         std::ranges::any_of(classic_magic, [bytes](std::string_view magic) {
           return bytes.starts_with(magic);
         });
}

std::expected<FileType, Error> netcdf_type(const std::filesystem::path& path,
                                           const ReadLimits& limits) {
  auto file = nc::File::open(path, limits);
  if (not file) {
    return std::unexpected{Error{std::move(file).error()}};
  }
  auto kind = detail::classify_netcdf(*file);
  if (auto closed = std::move(*file).close(); not closed and kind) {
    return std::unexpected{Error{std::move(closed).error()}};
  }
  if (not kind) {
    return std::unexpected{std::move(kind).error()};
  }
  switch (*kind) {
    case detail::NetcdfKind::station_v5:
      return FileType::station_netcdf;
    case detail::NetcdfKind::foreign_cf:
      return FileType::foreign_cf_netcdf;
    case detail::NetcdfKind::legacy_station:
      return FileType::legacy_station_netcdf;
    case detail::NetcdfKind::adcirc:
      return FileType::adcirc_netcdf;
    case detail::NetcdfKind::dflow:
      return FileType::dflow_netcdf;
    case detail::NetcdfKind::other:
      break;
  }
  return FileType::unknown;
}

// ---- text -------------------------------------------------------------------

/// The complete lines of `prefix` (a cut last line of a long file is
/// dropped), without line ends and without a byte order mark.
std::vector<std::string_view> lines_of(std::string_view prefix,
                                       bool truncated) {
  if (prefix.starts_with("\xEF\xBB\xBF")) {
    prefix.remove_prefix(3);
  }
  if (truncated) {
    prefix = prefix.substr(0, prefix.rfind('\n') + 1);
  }
  std::vector<std::string_view> lines;
  while (not prefix.empty() and lines.size() < sniff_lines) {
    const std::size_t end = prefix.find('\n');
    std::string_view line = prefix.substr(0, end);
    prefix.remove_prefix(end == std::string_view::npos ? prefix.size()
                                                       : end + 1);
    if (line.ends_with('\r')) {
      line.remove_suffix(1);
    }
    lines.push_back(line);
  }
  return lines;
}

bool all_digits(std::string_view word) {
  return not word.empty() and std::ranges::all_of(word, [](char c) {
    return c >= '0' and c <= '9';
  });
}

bool is_number(std::string_view word) {
  return detail::parse_double(word).has_value();
}

bool mentions_imeds(std::string_view line) {
  constexpr std::string_view needle = "imeds";
  for (std::size_t i = 0; i + needle.size() <= line.size(); ++i) {
    if (core::detail::equal_ignore_case(line.substr(i, needle.size()),
                                        needle)) {
      return true;
    }
  }
  return false;
}

/// `<name> <lat> <lon>`: an IMEDS station line.
bool is_station_line(std::string_view line) {
  const auto words = detail::split_ws<3>(line);
  return words and is_number((*words)[1]) and is_number((*words)[2]);
}

/// `yyyy mm dd hh mi [ss] value`: an IMEDS row.
bool is_row(std::string_view line) {
  const std::vector<std::string_view> words = detail::split_ws(line);
  if (words.size() != 6 and words.size() != 7) {
    return false;
  }
  const std::size_t date_words = words.size() - 1;
  return std::ranges::all_of(
             words.begin(),
             words.begin() + static_cast<std::ptrdiff_t>(date_words),
             all_digits) and
         is_number(words.back());
}

std::size_t skip_blank(const std::vector<std::string_view>& lines,
                       std::size_t from) {
  while (from < lines.size() and detail::is_blank(lines[from])) {
    ++from;
  }
  return from;
}

/// IMEDS: two free-text lines, a header, then blocks of a station line and its
/// rows (docs/legacy-formats.md 2.1). v4 wrote "% IMEDS generic format" first;
/// a file that does not is still IMEDS by the structure after the header.
bool looks_like_imeds(const std::vector<std::string_view>& lines) {
  if (not lines.empty() and mentions_imeds(lines.front())) {
    return true;
  }
  const std::size_t station = skip_blank(lines, 3);
  if (station >= lines.size() or not is_station_line(lines[station])) {
    return false;
  }
  const std::size_t next = skip_blank(lines, station + 1);
  return next >= lines.size() or is_row(lines[next]) or
         is_station_line(lines[next]);
}

/// ADCIRC ASCII output: a description line, the `NSnaps NStations DT NSPOOL
/// NCOLS` line, and the first snapshot's `time step` line.
bool looks_like_adcirc_ascii(const std::vector<std::string_view>& lines) {
  if (lines.size() < 3) {
    return false;
  }
  std::string head;
  for (const std::string_view line : {lines[0], lines[1]}) {
    head.append(line);
    head.push_back('\n');
  }
  if (not parse_adcirc_ascii_header(head).has_value()) {
    return false;
  }
  const auto words = detail::split_ws<2>(lines[2]);
  return words and is_number((*words)[0]) and all_digits((*words)[1]);
}

/// A high-water-mark CSV: its first rows parse.
bool looks_like_hwm(const std::vector<std::string_view>& lines) {
  std::string head;
  for (std::size_t i = 0; i < std::min(lines.size(), hwm_sniff_rows); ++i) {
    head.append(lines[i]);
    head.push_back('\n');
  }
  const auto parsed =
      parse_hwm_csv(head, core::LengthUnit::meter, ReadContext{});
  return parsed.has_value() and not parsed->value.empty();
}

FileType text_type(std::string_view prefix, bool truncated) {
  if (prefix.find('\0') != std::string_view::npos) {
    return FileType::unknown;  // binary
  }
  const std::vector<std::string_view> lines = lines_of(prefix, truncated);
  // ADCIRC first: its two-column rows look like IMEDS station lines.
  if (looks_like_adcirc_ascii(lines)) {
    return FileType::adcirc_ascii;
  }
  if (looks_like_imeds(lines)) {
    return FileType::imeds;
  }
  if (looks_like_hwm(lines)) {
    return FileType::hwm_csv;
  }
  return FileType::unknown;
}

}  // namespace

std::string_view to_token(FileType type) noexcept {
  switch (type) {
    case FileType::station_netcdf:
      return "station_netcdf";
    case FileType::foreign_cf_netcdf:
      return "foreign_cf_netcdf";
    case FileType::legacy_station_netcdf:
      return "legacy_station_netcdf";
    case FileType::adcirc_netcdf:
      return "adcirc_netcdf";
    case FileType::dflow_netcdf:
      return "dflow_netcdf";
    case FileType::imeds:
      return "imeds";
    case FileType::adcirc_ascii:
      return "adcirc_ascii";
    case FileType::hwm_csv:
      return "hwm_csv";
    case FileType::unknown:
      break;
  }
  return "unknown";
}

std::expected<FileType, Error> detect_file_type(
    const std::filesystem::path& path, const ReadLimits& limits) {
  auto prefix = read_text_prefix(path, sniff_bytes);
  if (not prefix) {
    return std::unexpected{Error{std::move(prefix).error()}};
  }
  if (is_netcdf_magic(*prefix)) {
    return netcdf_type(path, limits);
  }
  return text_type(*prefix, prefix->size() == sniff_bytes);
}

}  // namespace mov::io
