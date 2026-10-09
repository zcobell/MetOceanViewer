// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <numeric>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/tide/astronomy.hpp"
#include "mov/core/tide/node_factor.hpp"

// Constituent definitions and the Argument monoid
// (docs/harmonics-engine.md section 2.2, docs/harmonics-json.md section 7).

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
/// (no allocation, constexpr). Ordered by bytes, like the file's sort order.
/// Names are compared exactly here; a dictionary also refuses two names that
/// differ only in case (docs/harmonics-json.md section 7.1).
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
    if (not is_alnum(s.front()) or
        not std::ranges::all_of(s.substr(1), is_name_char)) {
      return std::unexpected(ConstituentNameError::bad_char);
    }
    ConstituentName n;
    std::ranges::copy(s, n.chars_.begin());
    n.size_ = static_cast<std::uint8_t>(s.size());
    return n;
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
  constexpr ConstituentName() noexcept = default;

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

enum class CompoundErrc : std::uint8_t {
  empty,             // no term
  too_many,          // more than max_terms
  zero_coefficient,  // a term with coefficient 0
  duplicate_base     // the same base twice
};

/// The terms of a compound constituent: base name -> non-zero coefficient,
/// 1 to 8 terms, sorted by name, no base twice. Whether each base is a
/// Darwin entry of the same dictionary is the dictionary's check (H2).
class CompoundTerms {
 public:
  static constexpr std::size_t max_terms = 8;

  struct Term {
    ConstituentName base;
    std::int8_t coefficient;
    friend constexpr bool operator==(const Term&, const Term&) = default;
  };

  /// Sorts the terms by base name; then the checks in CompoundErrc order.
  [[nodiscard]] static std::expected<CompoundTerms, CompoundErrc> make(
      std::vector<Term> terms);

  [[nodiscard]] std::span<const Term> terms() const& noexcept { return terms_; }
  std::span<const Term> terms() const&& = delete;  // would dangle

  friend bool operator==(const CompoundTerms&, const CompoundTerms&) = default;

 private:
  explicit CompoundTerms(std::vector<Term> terms) noexcept
      : terms_{std::move(terms)} {}
  std::vector<Term> terms_;
};

/// How a dictionary entry defines its constituent.
using Definition = std::variant<Darwin, CompoundTerms>;

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
/// Every coefficient is int32 and every exponent uint16, so flattening any
/// Definition is exact (at most 8 terms x 127 x 127, or 8 x 127 exponents);
/// sums beyond that are the caller's to avoid.
class Argument {
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

  // Plain index loops: cheap in constant evaluation too, where the catalogue
  // checks and the tests fold many of these within the compilers' step
  // budgets (Clang and MSVC: about one million).
  [[nodiscard]] friend constexpr Argument operator+(
      const Argument& a, const Argument& b) noexcept {
    Argument sum;
    for (std::size_t i = 0; i < sum.v_.size(); ++i) {
      sum.v_[i] = a.v_[i] + b.v_[i];
    }
    sum.c_deg_ = a.c_deg_ + b.c_deg_;
    for (std::size_t i = 0; i < sum.u_.size(); ++i) {
      sum.u_[i] = a.u_[i] + b.u_[i];
    }
    for (std::size_t i = 0; i < sum.exponent_.size(); ++i) {
      sum.exponent_[i] =
          static_cast<std::uint16_t>(a.exponent_[i] + b.exponent_[i]);
    }
    return sum;
  }

  [[nodiscard]] friend constexpr Argument scale(std::int8_t k,
                                                const Argument& a) noexcept {
    const std::int32_t factor = k;
    const auto magnitude = static_cast<std::uint16_t>(k < 0 ? -factor : factor);
    Argument scaled;
    for (std::size_t i = 0; i < scaled.v_.size(); ++i) {
      scaled.v_[i] = factor * a.v_[i];
    }
    scaled.c_deg_ = factor * a.c_deg_;
    for (std::size_t i = 0; i < scaled.u_.size(); ++i) {
      scaled.u_[i] = factor * a.u_[i];
    }
    for (std::size_t i = 0; i < scaled.exponent_.size(); ++i) {
      scaled.exponent_[i] =
          static_cast<std::uint16_t>(magnitude * a.exponent_[i]);
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
  [[nodiscard]] constexpr std::uint16_t exponent(NodeFactor f) const noexcept {
    return exponent_[static_cast<std::size_t>(f)];
  }
  /// The coefficient of T: cycles per (lunar or solar) day.
  [[nodiscard]] constexpr std::int32_t species() const noexcept {
    return v_[0];
  }

  /// Degrees per hour from the 1900 rates (docs/harmonics-json.md 7.2),
  /// including SP 98's M1 term b_Q * p-dot (paragraph 124).
  [[nodiscard]] constexpr double speed_deg_per_hour() const noexcept {
    const Elements& r = rates_1900;
    return dot5(r) + (static_cast<double>(u_[q_index]) * r.p);
  }

  /// V = v . (T, s, h, p, p1) + c at `at`, in degrees, not reduced.
  [[nodiscard]] constexpr double v_deg(const Elements& at) const noexcept {
    return dot5(at) + static_cast<double>(c_deg_);
  }

  /// u = u . (xi, nu, nu', 2nu'', Q, R, Qu) at `n`, in degrees, not reduced.
  [[nodiscard]] constexpr double u_deg(const NodalTerms& n) const noexcept {
    const std::array<double, 7> terms{n.xi, n.nu, n.nu1, n.two_nu2,
                                      n.Q,  n.R,  n.Qu};
    return std::inner_product(terms.begin(), terms.end(), u_.begin(), 0.0);
  }

  /// f = product over formulas of node_factor(formula, n)^exponent.
  [[nodiscard]] double f(const NodalTerms& n) const noexcept;

  friend constexpr bool operator==(const Argument&, const Argument&) = default;

 private:
  static constexpr std::size_t q_index = static_cast<std::size_t>(UTerm::Q);

  [[nodiscard]] constexpr double dot5(const Elements& e) const noexcept {
    return (static_cast<double>(v_[0]) * e.T) +
           (static_cast<double>(v_[1]) * e.s) +
           (static_cast<double>(v_[2]) * e.h) +
           (static_cast<double>(v_[3]) * e.p) +
           (static_cast<double>(v_[4]) * e.p1);
  }

  std::array<std::int32_t, 5> v_{};
  std::int32_t c_deg_{};
  std::array<std::int32_t, 7> u_{};
  std::array<std::uint16_t, node_factor_count> exponent_{};  // unity: 0
};

/// The Doodson coefficients (tau, s, h, p, N', p1) of `a`'s V: T = tau - s + h,
/// so they are (a_T, a_s + a_T, a_h - a_T, a_p, 0, a_p1)
/// (docs/harmonics-json.md section 7.2). c is not part of them.
[[nodiscard]] constexpr std::array<std::int32_t, 6> doodson_coefficients(
    const Argument& a) noexcept {
  const auto v = a.v();
  return {v[0], v[1] + v[0], v[2] - v[0], v[3], 0, v[4]};
}

/// The six-digit Doodson number (M2 255555, K1 165555, SA 56555, printed
/// 056.555): tau, then each other coefficient plus 5. nullopt when a digit
/// falls outside 0-9, as it does for higher compounds.
[[nodiscard]] constexpr std::optional<std::int32_t> doodson_number(
    const Argument& a) noexcept {
  const auto d = doodson_coefficients(a);
  std::int32_t number = 0;
  for (std::size_t i = 0; i < d.size(); ++i) {
    const std::int32_t digit = i == 0 ? d[i] : d[i] + 5;
    if (digit < 0 or digit > 9) {
      return std::nullopt;
    }
    number = (number * 10) + digit;
  }
  return number;
}

}  // namespace mov::core::tide
