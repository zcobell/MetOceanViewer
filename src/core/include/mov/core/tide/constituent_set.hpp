// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <cassert>
#include <compare>
#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/tide/constituent.hpp"
#include "mov/core/tide/positions.hpp"

// A harmonics file's constituent dictionary, resolved
// (docs/harmonics-engine.md section 2.3, docs/harmonics-json.md section 7):
// every entry flattened once into its Argument and speed, stored in the
// format's canonical order, and found by exact name.

namespace mov::core::detail {
struct HarmonicsAccess;  // builds the file's records (src/core/tide)
}  // namespace mov::core::detail

namespace mov::core::tide {

class ConstituentSet;

/// The position of a constituent in a ConstituentSet. Only a set makes one;
/// using it with another set is a precondition violation (asserted where it
/// would be out of range). Within a file, every index comes from the file's
/// one set.
class ConstituentIndex {
 public:
  [[nodiscard]] constexpr std::size_t value() const noexcept { return v_; }
  friend constexpr auto operator<=>(ConstituentIndex,
                                    ConstituentIndex) = default;

 private:
  friend class ConstituentSet;
  explicit constexpr ConstituentIndex(std::size_t v) noexcept : v_{v} {}
  std::size_t v_;
};
static_assert(std::is_trivially_copyable_v<ConstituentIndex>);

/// One dictionary entry as the file defines it. The file's `speed` is a
/// checksum, checked by HarmonicsFile::make, not part of the definition.
struct DictionaryEntry {
  ConstituentName name;
  Definition definition;
  std::optional<std::string> description;
  friend constexpr bool operator==(const DictionaryEntry&,
                                   const DictionaryEntry&) = default;
};

/// Entry `entry` has the name of entry `first` up to ASCII case
/// (E-DUPLICATE-CONSTITUENT); `first` is the earliest such entry.
struct DuplicateName {
  std::size_t entry;
  std::size_t first;
  friend constexpr bool operator==(const DuplicateName&,
                                   const DuplicateName&) = default;
};

/// Entry `entry` is a compound with a term that names no entry or a
/// compound (E-COMPOUND-TERM).
struct InvalidTerm {
  std::size_t entry;
  TermError error;
  friend constexpr bool operator==(const InvalidTerm&,
                                   const InvalidTerm&) = default;
};

/// Why a dictionary does not resolve. Indices are positions in the input.
using DictionaryError = std::variant<DuplicateName, InvalidTerm>;

}  // namespace mov::core::tide

namespace mov::core::detail {

/// For each entry, the earliest earlier entry whose name is equal to it
/// ignoring ASCII case (the dictionary's uniqueness rule,
/// docs/harmonics-json.md 7.1).
[[nodiscard]] constexpr std::vector<std::optional<std::size_t>>
same_name_ignoring_case(std::span<const tide::DictionaryEntry> entries) {
  return first_occurrence(
      entries.size(),
      [entries](std::size_t i) { return entries[i].name.view(); },
      less_ignoring_case);
}

/// Position -> exact name, for the sorted index below.
[[nodiscard]] constexpr auto entry_name_in(
    std::span<const tide::DictionaryEntry> entries) {
  return [entries](std::size_t i) { return entries[i].name.view(); };
}

/// Dictionary positions sorted by exact name. It holds no view of the
/// entries: every lookup is given them, so it cannot outlive them.
class EntryIndex {
 public:
  explicit constexpr EntryIndex(std::span<const tide::DictionaryEntry> entries)
      : by_name_{sorted_index(entries.size(), entry_name_in(entries))} {}

  /// The earliest entry named exactly `name`.
  [[nodiscard]] constexpr std::optional<std::size_t> find(
      std::span<const tide::DictionaryEntry> entries,
      std::string_view name) const {
    return find_sorted(by_name_, name, entry_name_in(entries));
  }

  /// The entries as a DefinitionLookup, for flatten.
  [[nodiscard]] constexpr auto lookup(
      std::span<const tide::DictionaryEntry> entries) const {
    return
        [this, entries](const tide::ConstituentName& n) -> tide::DefinitionRef {
          const auto i = find(entries, n.view());
          if (not i) {
            return std::nullopt;
          }
          return std::cref(entries[*i].definition);
        };
  }

 private:
  std::vector<std::size_t> by_name_;
};

}  // namespace mov::core::detail

namespace mov::core::tide {

/// A resolved dictionary: each entry with its flattened Argument and the
/// speed derived from it (docs/harmonics-json.md 7.2), in the format's
/// canonical order (species, then speed, then name; docs/harmonics-json.md
/// section 3), so two dictionaries with the same entries in any input order
/// are equal. Regular; there is no default (a file's dictionary has entries).
class ConstituentSet {
 public:
  /// Checks the entries in input order, each before the next: its name is
  /// not equal ignoring ASCII case to an earlier one (DuplicateName), then
  /// its definition flattens against the whole dictionary (InvalidTerm, for
  /// the first failing term in name order). The first failure is the error.
  [[nodiscard]] static constexpr std::expected<ConstituentSet, DictionaryError>
  make(std::vector<DictionaryEntry> entries);

  [[nodiscard]] constexpr std::size_t size() const noexcept {
    return entries_.size();
  }

  /// Every index, in canonical order (a view; no allocation).
  [[nodiscard]] constexpr auto indices() const noexcept {
    return std::views::iota(std::size_t{0}, entries_.size()) |
           std::views::transform(
               [](std::size_t i) { return ConstituentIndex{i}; });
  }

  /// The entry named exactly `name` (station rows use the exact spelling).
  [[nodiscard]] constexpr std::optional<ConstituentIndex> find(
      std::string_view exact) const {
    const auto i = detail::find_sorted(by_name_, exact, [this](std::size_t k) {
      return entries_[k].entry.name.view();
    });
    return i ? std::optional{ConstituentIndex{*i}} : std::nullopt;
  }

  [[nodiscard]] constexpr const ConstituentName& name(
      ConstituentIndex i) const& noexcept {
    return at(i).entry.name;
  }
  const ConstituentName& name(ConstituentIndex) const&& = delete;
  [[nodiscard]] constexpr const Definition& definition(
      ConstituentIndex i) const& noexcept {
    return at(i).entry.definition;
  }
  const Definition& definition(ConstituentIndex) const&& = delete;
  [[nodiscard]] constexpr const std::optional<std::string>& description(
      ConstituentIndex i) const& noexcept {
    return at(i).entry.description;
  }
  const std::optional<std::string>& description(ConstituentIndex) const&& =
      delete;
  [[nodiscard]] constexpr const Argument& argument(
      ConstituentIndex i) const& noexcept {
    return at(i).argument;
  }
  const Argument& argument(ConstituentIndex) const&& = delete;
  /// Degrees per hour, derived from the definition (the file's `speed` is a
  /// checksum).
  [[nodiscard]] constexpr double speed(ConstituentIndex i) const noexcept {
    return at(i).speed;
  }

  friend constexpr bool operator==(const ConstituentSet& a,
                                   const ConstituentSet& b) {
    return a.entries_ == b.entries_;
  }

 private:
  friend struct detail::HarmonicsAccess;

  struct Entry {
    DictionaryEntry entry;
    Argument argument;
    double speed;
    friend constexpr bool operator==(const Entry&, const Entry&) = default;
  };

  /// A set and, for each input entry, its index: from entries that passed
  /// make()'s rules and their flattened arguments (HarmonicsFile::make checks
  /// them itself, in its own order, and keeps the arguments).
  struct Ranked;
  [[nodiscard]] static constexpr Ranked ranked(
      std::vector<DictionaryEntry> entries,
      std::span<const Argument> arguments);

  explicit constexpr ConstituentSet(std::vector<Entry> sorted)
      : entries_{std::move(sorted)},
        by_name_{detail::sorted_index(entries_.size(), [this](std::size_t k) {
          return entries_[k].entry.name.view();
        })} {}

  /// An index of another set may be out of range: asserted.
  [[nodiscard]] constexpr const Entry& at(ConstituentIndex i) const noexcept {
    assert(i.value() < entries_.size());
    return entries_[i.value()];
  }

  std::vector<Entry> entries_;
  std::vector<std::size_t> by_name_;  // derived: positions sorted by name
};

struct ConstituentSet::Ranked {
  ConstituentSet set;
  std::vector<ConstituentIndex> index_of;  // by input position
};

constexpr ConstituentSet::Ranked ConstituentSet::ranked(
    std::vector<DictionaryEntry> entries, std::span<const Argument> arguments) {
  assert(entries.size() == arguments.size());
  const auto key = [&](std::size_t i) {
    return std::tuple{arguments[i].species(), arguments[i].speed_deg_per_hour(),
                      entries[i].name.view()};
  };
  const std::vector<std::size_t> order =
      detail::sorted_index(entries.size(), key);
  std::vector<Entry> sorted;
  sorted.reserve(entries.size());
  std::vector<ConstituentIndex> index_of(entries.size(), ConstituentIndex{0});
  for (std::size_t k = 0; k < order.size(); ++k) {
    const std::size_t i = order[k];
    index_of[i] = ConstituentIndex{k};
    sorted.push_back(Entry{.entry = std::move(entries[i]),
                           .argument = arguments[i],
                           .speed = arguments[i].speed_deg_per_hour()});
  }
  return Ranked{.set = ConstituentSet{std::move(sorted)},
                .index_of = std::move(index_of)};
}

constexpr std::expected<ConstituentSet, DictionaryError> ConstituentSet::make(
    std::vector<DictionaryEntry> entries) {
  const auto first = detail::same_name_ignoring_case(entries);
  const detail::EntryIndex index{entries};
  std::vector<Argument> arguments;
  arguments.reserve(entries.size());
  for (std::size_t i = 0; i < entries.size(); ++i) {
    if (const std::optional<std::size_t>& earlier = first[i]) {
      return std::unexpected(
          DictionaryError{DuplicateName{.entry = i, .first = *earlier}});
    }
    auto argument = flatten(entries[i].definition, index.lookup(entries));
    if (not argument) {
      return std::unexpected(
          DictionaryError{InvalidTerm{.entry = i, .error = argument.error()}});
    }
    arguments.push_back(*argument);
  }
  return ranked(std::move(entries), arguments).set;
}

}  // namespace mov::core::tide
