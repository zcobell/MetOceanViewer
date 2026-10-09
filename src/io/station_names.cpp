// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/detail/station_names.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "mov/core/utf8.hpp"

namespace mov::io::detail {

namespace {

constexpr std::string_view replacement_character{"\xEF\xBF\xBD"};

}  // namespace

CleanedText replace_invalid_utf8(std::string_view text) {
  std::string cleaned;
  bool replaced = false;
  cleaned.reserve(text.size());
  while (not text.empty()) {
    // A NUL is well-formed UTF-8, but no station key or name may hold one.
    const std::size_t length =
        text.front() == '\0' ? 0 : core::utf8_sequence_length(text);
    if (length == 0) {
      cleaned += replacement_character;
      replaced = true;
      text.remove_prefix(1);
    } else {
      cleaned += text.substr(0, length);
      text.remove_prefix(length);
    }
  }
  // Well-formed by construction, so make cannot fail; the empty fallback is
  // not reachable.
  return {.text = core::StationText::make(std::move(cleaned))
                      .value_or(core::StationText{}),
          .replaced = replaced};
}

UniqueIds uniquify_ids(std::span<const std::string> names) {
  UniqueIds out;
  out.ids.reserve(names.size());
  std::unordered_set<std::string> taken;
  taken.reserve(names.size());
  std::unordered_map<std::string, std::size_t> next_suffix;    // per name
  std::unordered_map<std::string, std::size_t> renamed_index;  // into renamed
  for (const std::string& name : names) {
    if (taken.insert(name).second) {
      out.ids.push_back(name);
      continue;
    }
    std::size_t& suffix = next_suffix.try_emplace(name, 2).first->second;
    std::string candidate;
    do {
      candidate = name + '#' + std::to_string(suffix++);
    } while (not taken.insert(candidate).second);
    out.ids.push_back(std::move(candidate));
    const auto [entry, first_time] =
        renamed_index.try_emplace(name, out.renamed.size());
    if (first_time) {
      out.renamed.push_back({.name = name, .count = 0});
    }
    ++out.renamed[entry->second].count;
  }
  return out;
}

}  // namespace mov::io::detail
