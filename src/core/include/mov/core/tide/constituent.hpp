// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>

#include "mov/core/ascii.hpp"
#include "mov/core/tide/astronomy.hpp"
#include "mov/core/tide/node_factor.hpp"
#include "mov/core/tide/phase.hpp"

// Constituent definitions, their flattening, and the Argument monoid
// (docs/harmonics-engine.md section 2.2, docs/harmonics-json.md section 7).
//
// Error enums: a value type's make() reports a <Type>Error enum (as
// TimeRangeError and StationKeyError do in core); an error that carries data
// is a struct or variant of structs.

namespace mov::core::tide {

/// A Darwin (SP 98) constituent: V = v . (T, s, h, p, p1) + c_deg, and
/// u = u . (xi, nu, nu', 2nu'', Q, R, Qu), with node-factor formula f. The
/// file schema bounds the coefficients (v[0] in [0, 16], the others in
/// [-64, 64], c in [-360, 360]); the type only bounds them by its fields.
struct Darwin {
  std::array<std::int8_t, 5> v;
  std::int16_t c_deg;
  std::array<std::int8_t, 7> u;
  NodeFactor f;
  friend constexpr bool operator==(const Darwin&, const Darwin&) = default;
};

/// Indices into Darwin::u and Argument::u(), in the order of the file.
enum class UTerm : std::uint8_t { xi, nu, nu1, two_nu2, Q, R, Qu };

enum class ConstituentNameError : std::uint8_t { empty, too_long, bad_char };

/// A constituent name: ^[A-Za-z0-9][A-Za-z0-9()_.+-]{0,31}$, stored inline
/// (no allocation, trivially copyable, constexpr). Ordered by bytes, like the
/// file's sort order. Names are compared exactly here; a dictionary also
/// refuses two names that differ only in case (docs/harmonics-json.md 7.1).
class ConstituentName {
 public:
  static constexpr std::size_t max_size = 32;

  [[nodiscard]] static constexpr std::expected<ConstituentName,
                                               ConstituentNameError>
  make(std::string_view s) noexcept {
    if (s.empty()) {
      return std::unexpected(ConstituentNameError::empty);
    }
    if (s.size() > max_size) {
      return std::unexpected(ConstituentNameError::too_long);
    }
    // Plain loops here and below: std::ranges costs many more steps in
    // constant evaluation, and the catalogue checks about 370 names there.
    if (not is_alnum(s.front())) {
      return std::unexpected(ConstituentNameError::bad_char);
    }
    for (const char c : s.substr(1)) {
      if (not is_name_char(c)) {
        return std::unexpected(ConstituentNameError::bad_char);
      }
    }
    return ConstituentName{s};
  }

  [[nodiscard]] constexpr std::string_view view() const& noexcept {
    return {chars_.data(), size_};
  }
  std::string_view view() const&& = delete;  // would dangle

  friend constexpr auto operator<=>(const ConstituentName&,
                                    const ConstituentName&) = default;
  friend constexpr bool operator==(const ConstituentName&,
                                   const ConstituentName&) = default;

 private:
  // Only make() calls it, with a name it has checked. (MSVC 19.44 cannot use
  // a private defaulted default constructor from the class's own static
  // member, hence a constructor that takes the text.)
  explicit constexpr ConstituentName(std::string_view validated) noexcept
      : size_{static_cast<std::uint8_t>(validated.size())} {
    for (std::size_t i = 0; i < validated.size(); ++i) {
      chars_[i] = validated[i];
    }
  }

  [[nodiscard]] static constexpr bool is_alnum(char c) noexcept {
    return (c >= 'A' and c <= 'Z') or (c >= 'a' and c <= 'z') or
           (c >= '0' and c <= '9');
  }
  [[nodiscard]] static constexpr bool is_name_char(char c) noexcept {
    return is_alnum(c) or c == '(' or c == ')' or c == '_' or c == '.' or
           c == '+' or c == '-';
  }

  // Unused bytes stay 0, so the defaulted <=> is byte order of view().
  std::array<char, max_size> chars_{};
  std::uint8_t size_{};
};

/// A name written in the source, checked at compile time: an invalid literal
/// does not compile. A factory function, not a consteval constructor, which
/// MSVC 19.44 rejects inside braced initialization.
[[nodiscard]] consteval ConstituentName constituent_name(std::string_view s) {
  return ConstituentName::make(s).value();
}

enum class CompoundTermsError : std::uint8_t {
  empty,             // no term
  too_many,          // more than max_terms
  zero_coefficient,  // a term with coefficient 0
  duplicate_base     // the same base twice (exact spelling)
};

/// The terms of a compound constituent: base name -> non-zero coefficient,
/// 1 to 8 terms, sorted by name, no base twice, held inline (trivially
/// copyable, constexpr). Whether each base is a Darwin entry of the same
/// dictionary is flatten's check. A JSON object can hold a member twice
/// (RFC 8259 only says names SHOULD be unique), so the reader passes every
/// member here and make() refuses the duplicate (docs/harmonics-engine.md
/// section 6.4).
class CompoundTerms {
 public:
  static constexpr std::size_t max_terms = 8;

  struct Term {
    ConstituentName base;
    std::int8_t coefficient;
    friend constexpr bool operator==(const Term&, const Term&) = default;
  };

  /// Checks empty, too_many and zero_coefficient on the input; then sorts
  /// the terms by base name and checks duplicate_base.
  [[nodiscard]] static constexpr std::expected<CompoundTerms,
                                               CompoundTermsError>
  make(std::span<const Term> terms) noexcept {
    if (terms.empty()) {
      return std::unexpected(CompoundTermsError::empty);
    }
    if (terms.size() > max_terms) {
      return std::unexpected(CompoundTermsError::too_many);
    }
    for (const Term& t : terms) {
      if (t.coefficient == 0) {
        return std::unexpected(CompoundTermsError::zero_coefficient);
      }
    }
    // Unused slots hold copies of the first term; only terms() is
    // observable, and == compares only that.
    const Term& first = terms.front();
    std::array<Term, max_terms> sorted{first, first, first, first,
                                       first, first, first, first};
    // Insertion sort by name, at most 8 terms: plainer than std::ranges::sort
    // and much cheaper in constant evaluation, which the catalogue uses.
    std::size_t n = 0;
    for (const Term& t : terms) {
      std::size_t i = n;
      while (i > 0 and t.base.view() < sorted[i - 1].base.view()) {
        sorted[i] = sorted[i - 1];
        --i;
      }
      sorted[i] = t;
      ++n;
    }
    for (std::size_t i = 1; i < n; ++i) {
      if (sorted[i].base.view() == sorted[i - 1].base.view()) {
        return std::unexpected(CompoundTermsError::duplicate_base);
      }
    }
    return CompoundTerms{sorted, static_cast<std::uint8_t>(n)};
  }

  [[nodiscard]] constexpr std::span<const Term> terms() const& noexcept {
    return {terms_.data(), size_};
  }
  std::span<const Term> terms() const&& = delete;  // would dangle

  friend constexpr bool operator==(const CompoundTerms& a,
                                   const CompoundTerms& b) noexcept {
    return std::ranges::equal(a.terms(), b.terms());
  }

 private:
  constexpr CompoundTerms(const std::array<Term, max_terms>& sorted,
                          std::uint8_t size) noexcept
      : terms_{sorted}, size_{size} {}

  std::array<Term, max_terms> terms_;
  std::uint8_t size_;
};

/// How a dictionary entry defines its constituent.
using Definition = std::variant<Darwin, CompoundTerms>;
static_assert(std::is_trivially_copyable_v<ConstituentName>);
static_assert(std::is_trivially_copyable_v<CompoundTerms>);
static_assert(std::is_trivially_copyable_v<Definition>);

}  // namespace mov::core::tide

namespace mov::core::detail {

/// a < b comparing ASCII letters without case (other bytes as they are),
/// then by length: the order in which a dictionary finds names that differ
/// only in case (docs/harmonics-json.md 7.1).
[[nodiscard]] constexpr bool less_ignoring_case(std::string_view a,
                                                std::string_view b) noexcept {
  const std::size_t n = a.size() < b.size() ? a.size() : b.size();
  for (std::size_t i = 0; i < n; ++i) {
    const char x = ascii::to_lower(a[i]);
    const char y = ascii::to_lower(b[i]);
    if (x != y) {
      return x < y;
    }
  }
  return a.size() < b.size();
}

/// Reached only when an Argument coefficient or exponent would leave its
/// type. Not constexpr, so constant evaluation stops there with an error; at
/// run time it asserts. Flattening any Definition cannot reach it (see
/// Argument).
inline void argument_overflow() noexcept {
  assert(false and "tide::Argument coefficient overflow");
}

[[nodiscard]] constexpr std::int32_t checked_int32(std::int64_t v) noexcept {
  if (v < std::numeric_limits<std::int32_t>::min() or
      v > std::numeric_limits<std::int32_t>::max()) {
    argument_overflow();
  }
  return static_cast<std::int32_t>(v);
}

[[nodiscard]] constexpr std::uint32_t checked_uint32(std::uint64_t v) noexcept {
  if (v > std::numeric_limits<std::uint32_t>::max()) {
    argument_overflow();
  }
  return static_cast<std::uint32_t>(v);
}

}  // namespace mov::core::detail

namespace mov::core::tide {

/// The recipe of one constituent's equilibrium argument and node factor,
/// flattened: integer coefficients of the elements and of the u terms, the
/// constant c, and an exponent per node-factor formula.
///
/// (Argument, +, Argument{}) is a commutative monoid: + adds the V, c and u
/// coefficients and adds the node-factor exponents, because f is a product
/// (f(a + b) = f(a) f(b)). scale(k, a) multiplies V, c and u by k but the
/// exponents by |k|: f is multiplied, never divided (IHO Standard List of
/// Tidal Constituents, Annex B; libcongen's operator*=). Scaling is therefore
/// not a module action: scale(k, a) + scale(-k, a) is the identity only when
/// a has no node factor. A compound is the fold sum_i scale(c_i, darwin(b_i)).
///
/// Coefficients are int32 and exponents uint32. Flattening any Definition
/// stays far inside them (at most 8 terms x 128 x 32768 for c); + and scale
/// check every result, so an overflow is a compile error in constant
/// evaluation and an assertion at run time.
class Argument {
 private:
  // Declared before their first use: Clang cannot constant-evaluate a
  // member function template that a member function body above its
  // definition calls ("undefined function").
  static constexpr std::size_t q_index = static_cast<std::size_t>(UTerm::Q);

  [[nodiscard]] static constexpr double degrees_of(double x) noexcept {
    return x;
  }
  [[nodiscard]] static constexpr double degrees_of(Phase x) noexcept {
    return x.degrees();
  }

  /// v . (T, s, h, p, p1) for positions (Elements) or rates (ElementRates).
  template <class E>
  [[nodiscard]] constexpr double dot5(const E& e) const noexcept {
    return (static_cast<double>(v_[0]) * degrees_of(e.T)) +
           (static_cast<double>(v_[1]) * degrees_of(e.s)) +
           (static_cast<double>(v_[2]) * degrees_of(e.h)) +
           (static_cast<double>(v_[3]) * degrees_of(e.p)) +
           (static_cast<double>(v_[4]) * degrees_of(e.p1));
  }

 public:
  constexpr Argument() noexcept = default;  // the identity

  [[nodiscard]] static constexpr Argument darwin(const Darwin& d) noexcept {
    Argument a;
    std::ranges::copy(d.v, a.v_.begin());
    a.c_deg_ = d.c_deg;
    std::ranges::copy(d.u, a.u_.begin());
    if (d.f != NodeFactor::unity) {
      a.exponent_[static_cast<std::size_t>(d.f)] = 1;
    }
    return a;
  }

  // Plain index loops: cheap in constant evaluation, where the catalogue
  // checks and the tests fold many of these within the compilers' step
  // budgets (MSVC documents a default of 100,000 steps).
  [[nodiscard]] friend constexpr Argument operator+(
      const Argument& a, const Argument& b) noexcept {
    using detail::checked_int32;
    Argument sum;
    for (std::size_t i = 0; i < sum.v_.size(); ++i) {
      sum.v_[i] = checked_int32(std::int64_t{a.v_[i]} + b.v_[i]);
    }
    sum.c_deg_ = checked_int32(std::int64_t{a.c_deg_} + b.c_deg_);
    for (std::size_t i = 0; i < sum.u_.size(); ++i) {
      sum.u_[i] = checked_int32(std::int64_t{a.u_[i]} + b.u_[i]);
    }
    for (std::size_t i = 0; i < sum.exponent_.size(); ++i) {
      sum.exponent_[i] = detail::checked_uint32(std::uint64_t{a.exponent_[i]} +
                                                b.exponent_[i]);
    }
    return sum;
  }

  [[nodiscard]] friend constexpr Argument scale(std::int8_t k,
                                                const Argument& a) noexcept {
    using detail::checked_int32;
    const std::int64_t factor = k;
    const std::uint64_t magnitude =
        static_cast<std::uint64_t>(k < 0 ? -factor : factor);
    Argument scaled;
    for (std::size_t i = 0; i < scaled.v_.size(); ++i) {
      scaled.v_[i] = checked_int32(factor * a.v_[i]);
    }
    scaled.c_deg_ = checked_int32(factor * a.c_deg_);
    for (std::size_t i = 0; i < scaled.u_.size(); ++i) {
      scaled.u_[i] = checked_int32(factor * a.u_[i]);
    }
    for (std::size_t i = 0; i < scaled.exponent_.size(); ++i) {
      scaled.exponent_[i] = detail::checked_uint32(magnitude * a.exponent_[i]);
    }
    return scaled;
  }

  /// Coefficients of T, s, h, p, p1.
  [[nodiscard]] constexpr std::array<std::int32_t, 5> v() const noexcept {
    return v_;
  }
  [[nodiscard]] constexpr std::int32_t c_deg() const noexcept { return c_deg_; }
  /// Coefficients of xi, nu, nu', 2nu'', Q, R, Qu (indexed by UTerm).
  [[nodiscard]] constexpr std::array<std::int32_t, 7> u() const noexcept {
    return u_;
  }
  /// The exponent of formula `f` in f(a); always 0 for unity.
  [[nodiscard]] constexpr std::uint32_t exponent(NodeFactor f) const noexcept {
    return exponent_[static_cast<std::size_t>(f)];
  }
  /// The coefficient of T: cycles per (lunar or solar) day.
  [[nodiscard]] constexpr std::int32_t species() const noexcept {
    return v_[0];
  }

  /// Degrees per hour from the 1900 rates (docs/harmonics-json.md 7.2),
  /// including SP 98's M1 term b_Q * p-dot (paragraph 124).
  [[nodiscard]] constexpr double speed_deg_per_hour() const noexcept {
    return dot5(rates_1900) + (static_cast<double>(u_[q_index]) * rates_1900.p);
  }

  /// V = v . (T, s, h, p, p1) + c at `at`, in degrees, not reduced.
  [[nodiscard]] constexpr double v_deg(const Elements& at) const noexcept {
    return dot5(at) + static_cast<double>(c_deg_);
  }

  /// u = u . (xi, nu, nu', 2nu'', Q, R, Qu) at `n`, in degrees, not reduced.
  [[nodiscard]] constexpr double u_deg(const NodalTerms& n) const noexcept {
    const std::array<double, 7> terms{n.xi, n.nu, n.nu1, n.two_nu2,
                                      n.Q,  n.R,  n.Qu};
    double sum = 0.0;
    for (std::size_t i = 0; i < terms.size(); ++i) {
      sum += static_cast<double>(u_[i]) * terms[i];
    }
    return sum;
  }

  /// f = product over formulas of node_factor(formula, n)^exponent.
  [[nodiscard]] double f(const NodalTerms& n) const noexcept;

  /// The same product from values already evaluated for the year: the same
  /// multiplications in the same order, so the same bits as f(n) when
  /// `values` is NodeFactorValues::of(n).
  [[nodiscard]] constexpr double f(
      const NodeFactorValues& values) const noexcept {
    double product = 1.0;
    for (const NodeFactor formula : all_node_factors) {
      const std::uint32_t e = exponent(formula);
      for (std::uint32_t k = 0; k < e; ++k) {
        product *= values[formula];
      }
    }
    return product;
  }

  friend constexpr bool operator==(const Argument&, const Argument&) = default;

 private:
  std::array<std::int32_t, 5> v_{};
  std::int32_t c_deg_{};
  std::array<std::int32_t, 7> u_{};
  std::array<std::uint32_t, node_factor_count> exponent_{};  // unity: 0
};
static_assert(std::is_trivially_copyable_v<Argument>);

/// A compound term that names no entry of the dictionary.
struct UnknownTerm {
  ConstituentName term;
  friend constexpr bool operator==(const UnknownTerm&,
                                   const UnknownTerm&) = default;
};
/// A compound term that names a compound, not a Darwin entry: no nesting.
struct TermNotDarwin {
  ConstituentName term;
  friend constexpr bool operator==(const TermNotDarwin&,
                                   const TermNotDarwin&) = default;
};
/// Why a Definition does not flatten. A dictionary (H2) adds the entry's
/// index to report it.
using TermError = std::variant<UnknownTerm, TermNotDarwin>;

/// What a dictionary lookup returns: the definition of a name, if it has one.
using DefinitionRef = std::optional<std::reference_wrapper<const Definition>>;

/// A dictionary lookup by exact name.
template <class F>
concept DefinitionLookup =
    std::is_invocable_r_v<DefinitionRef, const F&, const ConstituentName&>;

/// The Argument of `d`: a Darwin entry directly, a compound as the fold of
/// its scaled terms, each of which `definition_of` must resolve to a Darwin
/// entry. One implementation for the built-in catalogue and the file's
/// dictionary.
template <DefinitionLookup F>
[[nodiscard]] constexpr std::expected<Argument, TermError> flatten(
    const Definition& d, const F& definition_of) noexcept {
  if (std::holds_alternative<Darwin>(d)) {
    return Argument::darwin(std::get<Darwin>(d));
  }
  Argument sum;
  for (const CompoundTerms::Term& t : std::get<CompoundTerms>(d).terms()) {
    const DefinitionRef base = definition_of(t.base);
    if (not base) {
      return std::unexpected(TermError{UnknownTerm{.term = t.base}});
    }
    const Definition& b = base->get();
    if (not std::holds_alternative<Darwin>(b)) {
      return std::unexpected(TermError{TermNotDarwin{.term = t.base}});
    }
    sum = sum + scale(t.coefficient, Argument::darwin(std::get<Darwin>(b)));
  }
  return sum;
}

/// The Doodson coefficients (tau, s, h, p, N', p1) of `a`'s V: T = tau - s + h,
/// so they are (a_T, a_s + a_T, a_h - a_T, a_p, 0, a_p1)
/// (docs/harmonics-json.md section 7.2). c is not part of them.
[[nodiscard]] constexpr std::array<std::int32_t, 6> doodson_coefficients(
    const Argument& a) noexcept {
  const auto v = a.v();
  return {v[0], v[1] + v[0], v[2] - v[0], v[3], 0, v[4]};
}

/// The six digits of the Doodson number: tau, then each other coefficient
/// plus 5 (M2 "255555", SA "056555", printed 056.555). nullopt when a digit
/// falls outside 0-9, as it does for higher compounds.
[[nodiscard]] constexpr std::optional<std::array<char, 6>> doodson_number(
    const Argument& a) noexcept {
  const auto d = doodson_coefficients(a);
  std::array<char, 6> digits{};
  for (std::size_t i = 0; i < d.size(); ++i) {
    const std::int32_t digit = i == 0 ? d[i] : d[i] + 5;
    if (digit < 0 or digit > 9) {
      return std::nullopt;
    }
    digits[i] = static_cast<char>('0' + digit);
  }
  return digits;
}

}  // namespace mov::core::tide
