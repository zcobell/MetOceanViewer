// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "mov/io/netcdf/name.hpp"

namespace mov::io::nc {

/// The external type of a netCDF variable or attribute. `other` is anything
/// the wrapper does not read: a user-defined compound, enum, opaque or vlen
/// type.
enum class Type : std::uint8_t {
  byte,
  ubyte,
  char_,
  short_,
  ushort,
  int_,
  uint,
  int64,
  uint64,
  float_,
  double_,
  string,
  other,
};

/// The memory types the wrapper reads and writes numbers as. Unsigned types
/// are not among them: no known source uses them, and a read of one is
/// refused rather than reinterpreted (design section 4.3).
template <class T>
concept Numeric =
    std::same_as<T, double> or std::same_as<T, float> or
    std::same_as<T, std::int8_t> or std::same_as<T, std::int16_t> or
    std::same_as<T, std::int32_t> or std::same_as<T, std::int64_t>;

/// The netCDF type whose values are exactly the values of T.
template <Numeric T>
inline constexpr Type type_of = [] {
  if constexpr (std::same_as<T, double>) {
    return Type::double_;
  } else if constexpr (std::same_as<T, float>) {
    return Type::float_;
  } else if constexpr (std::same_as<T, std::int8_t>) {
    return Type::byte;
  } else if constexpr (std::same_as<T, std::int16_t>) {
    return Type::short_;
  } else if constexpr (std::same_as<T, std::int32_t>) {
    return Type::int_;
  } else {
    return Type::int64;
  }
}();

namespace detail {

/// The column of T in read_table.
template <Numeric T>
inline constexpr std::size_t read_column = [] {
  if constexpr (std::same_as<T, double>) {
    return 0;
  } else if constexpr (std::same_as<T, float>) {
    return 1;
  } else if constexpr (std::same_as<T, std::int8_t>) {
    return 2;
  } else if constexpr (std::same_as<T, std::int16_t>) {
    return 3;
  } else if constexpr (std::same_as<T, std::int32_t>) {
    return 4;
  } else {
    return 5;
  }
}();

/// The table of design section 4.3, one row per Type (in its order), one
/// column per memory type: double, float, int8, int16, int32, int64.
inline constexpr std::array<std::array<bool, 6>, 13> read_table{{
    {true, false, true, true, true, true},      // byte
    {},                                         // ubyte
    {},                                         // char_
    {true, false, false, true, true, true},     // short_
    {},                                         // ushort
    {true, false, false, false, true, true},    // int_
    {},                                         // uint
    {true, false, false, false, false, true},   // int64 (to double: time)
    {},                                         // uint64
    {true, true, false, false, false, false},   // float_
    {true, false, false, false, false, false},  // double_
    {},                                         // string
    {},                                         // other
}};

}  // namespace detail

/// Whether `read<T>` accepts a variable of type `from` (design section 4.3):
/// only conversions that keep every value, so never double to float (B4).
/// int64 widens to double for time variables only, whose callers check the
/// range with checked_time.
template <Numeric T>
[[nodiscard]] constexpr bool readable_as(Type from) noexcept {
  return detail::read_table.at(static_cast<std::size_t>(from))
      .at(detail::read_column<T>);
}

/// Whether read_samples, and a reader built on it, can read a variable of type
/// `t`: the types whose every value a double holds, masked in their own type.
/// 64-bit integers are not among them (time variables read int64_t instead),
/// nor are unsigned, text and user-defined types.
[[nodiscard]] constexpr bool sample_readable(Type t) noexcept {
  return t == Type::byte or t == Type::short_ or t == Type::int_ or
         t == Type::float_ or t == Type::double_;
}

/// Calls `on_numeric.template operator()<T>()` with the Numeric T whose
/// netCDF type is `t`, or `on_other()` for the types that have none. Both must
/// return the same type.
template <class F, class G>
decltype(auto) dispatch_numeric(Type t, F&& on_numeric, G&& on_other) {
  switch (t) {
    case Type::double_:
      return std::forward<F>(on_numeric).template operator()<double>();
    case Type::float_:
      return std::forward<F>(on_numeric).template operator()<float>();
    case Type::byte:
      return std::forward<F>(on_numeric).template operator()<std::int8_t>();
    case Type::short_:
      return std::forward<F>(on_numeric).template operator()<std::int16_t>();
    case Type::int_:
      return std::forward<F>(on_numeric).template operator()<std::int32_t>();
    case Type::int64:
      return std::forward<F>(on_numeric).template operator()<std::int64_t>();
    case Type::ubyte:
    case Type::char_:
    case Type::ushort:
    case Type::uint:
    case Type::uint64:
    case Type::string:
    case Type::other:
      break;
  }
  return std::forward<G>(on_other)();
}

struct DimInfo {
  int id;
  NcName name;
  std::size_t length;  // the current length of an unlimited dimension
  friend bool operator==(const DimInfo&, const DimInfo&) = default;
};

struct VarInfo {
  int id;
  NcName name;
  Type type;
  std::vector<DimInfo> dims;  // outermost first
  friend bool operator==(const VarInfo&, const VarInfo&) = default;
};

/// The part of one dimension a read or write covers: `count` elements from
/// index `start`.
struct DimRange {
  std::size_t start;
  std::size_t count;
  friend constexpr bool operator==(DimRange, DimRange) = default;
};

/// A hyperslab: one DimRange per dimension of the variable, outermost first
/// (a scalar takes an empty Slab). A rank that differs from the variable's
/// is `rank_mismatch`.
using Slab = std::vector<DimRange>;

/// The whole of every dimension of `var`.
[[nodiscard]] inline Slab whole(const VarInfo& var) {
  Slab slab(var.dims.size());
  std::ranges::transform(var.dims, slab.begin(), [](const DimInfo& dim) {
    return DimRange{.start = 0, .count = dim.length};
  });
  return slab;
}

/// The outer indices (of the first dimension) each block of a bulk read
/// covers: as many as fit in `slab_elements` elements, at least one. Every
/// block holds whole rows of the inner dimensions, so a block is larger than
/// `slab_elements` only when one outer index alone is. A scalar is one
/// block. File::read_blocks cuts slabs this way; callers that size their
/// own work use it rather than re-deriving the partition.
[[nodiscard]] constexpr std::size_t rows_per_block(
    const Slab& slab, std::size_t slab_elements) noexcept {
  if (slab.empty()) {
    return 1;
  }
  std::size_t inner = 1;
  for (std::size_t i = 1; i < slab.size(); ++i) {
    inner = slab[i].count == 0 or inner <= slab_elements / slab[i].count
                ? inner * slab[i].count
                : std::numeric_limits<std::size_t>::max();  // over a block
  }
  return std::max<std::size_t>(1, inner == 0 ? 1 : slab_elements / inner);
}

namespace detail {

/// A Slab as the two arrays netCDF-C takes.
struct Hyperslab {
  std::vector<std::size_t> start;
  std::vector<std::size_t> count;
};

[[nodiscard]] inline Hyperslab unzip(const Slab& slab) {
  Hyperslab h{.start = std::vector<std::size_t>(slab.size()),
              .count = std::vector<std::size_t>(slab.size())};
  std::ranges::transform(slab, h.start.begin(), &DimRange::start);
  std::ranges::transform(slab, h.count.begin(), &DimRange::count);
  return h;
}

}  // namespace detail

/// The attribute owner that is the file itself.
struct Global {
  friend constexpr bool operator==(Global, Global) = default;
};
inline constexpr Global global{};

/// Where an attribute lives: on the file (`nc::global`) or on a variable,
/// named like any other NcNameRef (a literal is checked at compile time).
class AttTarget {
 public:
  constexpr AttTarget(Global /*unused*/) noexcept {}
  constexpr AttTarget(NcNameRef variable) noexcept : variable_{variable} {}
  template <std::size_t N>
  // NOLINTNEXTLINE(modernize-avoid-c-arrays): a string literal is the input
  consteval AttTarget(const char (&variable)[N])
      : variable_{NcNameRef{variable}} {}
  AttTarget(const NcName& variable) noexcept : variable_{variable} {}
  AttTarget(const NcName&&) = delete;

  /// The variable, or nullopt for a global attribute.
  [[nodiscard]] constexpr std::optional<NcNameRef> variable() const noexcept {
    return variable_;
  }

  friend constexpr bool operator==(const AttTarget&,
                                   const AttTarget&) = default;

 private:
  std::optional<NcNameRef> variable_{};
};

/// How define_var sets a new variable up. `fill` becomes its _FillValue;
/// `deflate_level` (0 to 9) turns on shuffle and deflate; `chunks` (one per
/// dimension) makes the storage chunked.
template <class T>
struct VarOptions {
  std::optional<T> fill{};
  std::optional<int> deflate_level{};
  std::optional<std::vector<std::size_t>> chunks{};
};

}  // namespace mov::io::nc
