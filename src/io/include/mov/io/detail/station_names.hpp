// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Cleaning and uniquifying the station names of lenient text sources.
// Private to mov::io (public only because tests and fuzz targets include it).

#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "mov/core/station.hpp"

namespace mov::io::detail {

/// `text` as station text (well-formed UTF-8 without NUL): every byte that is
/// not part of a well-formed sequence, and every NUL, becomes U+FFFD.
/// `replaced` says whether any did.
struct CleanedText {
  core::StationText text;
  bool replaced;
  friend bool operator==(const CleanedText&, const CleanedText&) = default;
};
[[nodiscard]] CleanedText replace_invalid_utf8(std::string_view text);

/// A name that was seen more than once and how many of its occurrences were
/// renamed.
struct RenamedName {
  std::string name;
  std::size_t count;
  friend bool operator==(const RenamedName&, const RenamedName&) = default;
};

/// Ids parallel to the names, all distinct.
struct UniqueIds {
  std::vector<std::string> ids;
  std::vector<RenamedName> renamed;  // in order of first rename
  friend bool operator==(const UniqueIds&, const UniqueIds&) = default;
};

/// The first occurrence of a name keeps it; the n-th later one becomes
/// `name#n+1` (`A`, `A#2`, `A#3`), skipping any id already taken, so a file
/// that has a station literally called `A#2` still gets distinct ids.
[[nodiscard]] UniqueIds uniquify_ids(std::span<const std::string> names);

}  // namespace mov::io::detail
