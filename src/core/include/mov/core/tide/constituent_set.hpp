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
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/tide/constituent.hpp"

// A harmonics file's constituent dictionary, resolved
// (docs/harmonics-engine.md section 2.3, docs/harmonics-json.md section 7):
// every entry flattened once into its Argument and speed, stored in the
// format's canonical order, and found by exact name.

namespace mov::core::tide {

class ConstituentSet;

/// The position of a constituent in one ConstituentSet. Only the set makes
/// one, so an index always names an entry of the set it came from.
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
/// ignoring ASCII case, if there is one. Sorted once: O(n log n).
[[nodiscard]] constexpr std::vector<std::optional<std::size_t>>
earlier_same_name(std::span<const tide::DictionaryEntry> entries) {
  std::vector<std::size_t> order(entries.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  const auto name_of = [entries](std::size_t i) {
    return entries[i].name.view();
  };
  // By position within a group of equal names, so the group's first element
  // is its earliest entry. (std::stable_sort is constexpr only from C++26.)
  std::ranges::sort(order, [&](std::size_t a, std::size_t b) {
    if (less_ignoring_case(name_of(a), name_of(b))) {
      return true;
    }
    return not less_ignoring_case(name_of(b), name_of(a)) and a < b;
  });
  std::vector<std::optional<std::size_t>> first(entries.size());
  std::size_t group = 0;
  for (std::size_t k = 1; k < order.size(); ++k) {
    if (less_ignoring_case(name_of(order[group]), name_of(order[k]))) {
      group = k;
    } else {
      first[order[k]] = order[group];
    }
  }
  return first;
}

/// Exact-name lookup over dictionary entries, through an index sorted by
/// name; with equal names, the earliest entry. Holds a view: `entries` must
/// outlive it.
class EntryLookup {
 public:
  explicit constexpr EntryLookup(std::span<const tide::DictionaryEntry> entries)
      : entries_{entries}, by_name_(entries.size()) {
    for (std::size_t i = 0; i < by_name_.size(); ++i) {
      by_name_[i] = i;
    }
    // By name, then position: the earliest of equal names comes first.
    std::ranges::sort(by_name_, std::less{}, [this](std::size_t i) {
      return std::pair{entries_[i].name.view(), i};
    });
  }

  /// The position of the entry named exactly `name`.
  [[nodiscard]] constexpr std::optional<std::size_t> index(
      std::string_view name) const {
    const auto found = std::ranges::lower_bound(
        by_name_, name, std::less{},
        [this](std::size_t i) { return entries_[i].name.view(); });
    if (found == by_name_.end() or entries_[*found].name.view() != name) {
      return std::nullopt;
    }
    return *found;
  }

  /// The lookup as a DefinitionLookup, for flatten.
  [[nodiscard]] constexpr tide::DefinitionRef operator()(
      const tide::ConstituentName& name) const {
    const auto i = index(name.view());
    if (not i) {
      return std::nullopt;
    }
    return std::cref(entries_[*i].definition);
  }

 private:
  std::span<const tide::DictionaryEntry> entries_;
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
  make(std::vector<DictionaryEntry> entries) {
    const auto first = detail::earlier_same_name(entries);
    const detail::EntryLookup lookup{entries};
    std::vector<Argument> arguments;
    arguments.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
      if (first[i]) {
        return std::unexpected(
            DictionaryError{DuplicateName{.entry = i, .first = *first[i]}});
      }
      auto argument = flatten(entries[i].definition, lookup);
      if (not argument) {
        return std::unexpected(DictionaryError{
            InvalidTerm{.entry = i, .error = argument.error()}});
      }
      arguments.push_back(*argument);
    }
    std::vector<Entry> resolved;
    resolved.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
      resolved.push_back(Entry{.entry = std::move(entries[i]),
                               .argument = arguments[i],
                               .speed = arguments[i].speed_deg_per_hour()});
    }
    std::ranges::sort(resolved, [](const Entry& a, const Entry& b) {
      return std::tuple{a.argument.species(), a.speed, a.entry.name.view()} <
             std::tuple{b.argument.species(), b.speed, b.entry.name.view()};
    });
    return ConstituentSet{std::move(resolved)};
  }

  [[nodiscard]] constexpr std::size_t size() const noexcept {
    return entries_.size();
  }

  /// Every index, in canonical order.
  [[nodiscard]] constexpr std::vector<ConstituentIndex> indices() const {
    std::vector<ConstituentIndex> all;
    all.reserve(entries_.size());
    for (std::size_t i = 0; i < entries_.size(); ++i) {
      all.push_back(ConstituentIndex{i});
    }
    return all;
  }

  /// The entry named exactly `name` (station rows use the exact spelling).
  [[nodiscard]] constexpr std::optional<ConstituentIndex> find(
      std::string_view exact) const {
    const auto found = std::ranges::lower_bound(
        by_name_, exact, std::less{},
        [this](ConstituentIndex i) { return name(i).view(); });
    if (found == by_name_.end() or name(*found).view() != exact) {
      return std::nullopt;
    }
    return *found;
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
  struct Entry {
    DictionaryEntry entry;
    Argument argument;
    double speed;
    friend constexpr bool operator==(const Entry&, const Entry&) = default;
  };

  explicit constexpr ConstituentSet(std::vector<Entry> entries)
      : entries_{std::move(entries)} {
    by_name_ = indices();
    std::ranges::sort(by_name_, std::less{},
                      [this](ConstituentIndex i) { return name(i).view(); });
  }

  /// An index of another set may be out of range: asserted.
  [[nodiscard]] constexpr const Entry& at(ConstituentIndex i) const noexcept {
    assert(i.value() < entries_.size());
    return entries_[i.value()];
  }

  std::vector<Entry> entries_;
  std::vector<ConstituentIndex> by_name_;  // derived from entries_
};

}  // namespace mov::core::tide
