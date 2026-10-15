// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/core/ascii.hpp"

// Licence expressions of a harmonics file (docs/harmonics-json.md 6.1,
// docs/harmonics-engine.md 2.4): the SPDX subset of licence identifiers,
// AND, OR, WITH and parentheses, tokenized rather than searched as text.

namespace mov::core::tide {

enum class LicenceErrc : std::uint8_t {
  empty,              // nothing but spaces
  bad_character,      // a byte outside [A-Za-z0-9.+-], '(', ')' and ' ', or
                      // outside [A-Za-z0-9.-] after "LicenseRef-"
  expected_licence,   // an operator, ')' or the end where an identifier or
                      // '(' belongs (after WITH: where an exception belongs)
  expected_operator,  // an identifier or '(' after an operand, or WITH not
                      // directly after a licence identifier
  unbalanced          // a ')' without its '(', or a '(' never closed
};

/// Where and why an expression does not parse. `offset` is the byte of the
/// offending token, or the length of the text when the text ends too soon.
struct LicenceError {
  std::size_t offset;
  LicenceErrc code;
  friend constexpr bool operator==(const LicenceError&,
                                   const LicenceError&) = default;
};

}  // namespace mov::core::tide

namespace mov::core::detail {

[[nodiscard]] constexpr bool is_licence_char(char c) noexcept {
  return ascii::is_alnum(c) or c == '.' or c == '+' or c == '-';
}

inline constexpr std::string_view licence_ref_prefix = "LicenseRef-";

/// The offset of the first byte of `id` (at `offset` in the text) that the
/// LicenseRef- grammar refuses, if it is a LicenseRef identifier: the idstring
/// after the prefix is [A-Za-z0-9.-]+.
[[nodiscard]] constexpr std::optional<std::size_t> bad_licence_ref(
    std::string_view id, std::size_t offset) noexcept {
  if (not id.starts_with(licence_ref_prefix)) {
    return std::nullopt;
  }
  const std::string_view rest = id.substr(licence_ref_prefix.size());
  if (rest.empty()) {
    return offset + id.size();
  }
  for (std::size_t i = 0; i < rest.size(); ++i) {
    if (rest[i] == '+') {
      return offset + licence_ref_prefix.size() + i;
    }
  }
  return std::nullopt;
}

/// Whether `id` has a component, between '-'s, equal to "NC" ignoring case.
[[nodiscard]] constexpr bool has_nc_component(std::string_view id) noexcept {
  while (true) {
    const std::size_t dash = id.find('-');
    if (ascii::equal_ignore_case(id.substr(0, dash), "NC")) {
      return true;
    }
    if (dash == std::string_view::npos) {
      return false;
    }
    id.remove_prefix(dash + 1);
  }
}

/// Licence::parse's state machine: one pass over the text, a token at a
/// time, with the nesting depth as a counter (no recursion, so no input can
/// exhaust the stack).
class LicenceParser {
 public:
  explicit constexpr LicenceParser(std::string_view text) noexcept
      : text_{text} {}

  /// nullopt when the whole text parses; the tokens are then complete.
  [[nodiscard]] constexpr std::optional<tide::LicenceError> run() {
    while (at_ < text_.size()) {
      if (const auto error = step()) {
        return error;
      }
    }
    return finish();
  }

  [[nodiscard]] constexpr std::vector<std::string> identifiers() && noexcept {
    return std::move(identifiers_);
  }
  [[nodiscard]] constexpr std::vector<std::string> exceptions() && noexcept {
    return std::move(exceptions_);
  }

 private:
  using Errc = tide::LicenceErrc;
  using Error = std::optional<tide::LicenceError>;
  // What the next token may be: a licence or '(' (operand), AND, OR, WITH
  // or ')' (operator_), the identifier after WITH (exception).
  enum class Expect : std::uint8_t { operand, operator_, exception };

  [[nodiscard]] static constexpr Error fail(std::size_t offset,
                                            Errc code) noexcept {
    return tide::LicenceError{.offset = offset, .code = code};
  }

  constexpr Error step() {
    const char c = text_[at_];
    if (c == ' ') {
      ++at_;
      return std::nullopt;
    }
    if (c == '(') {
      return open();
    }
    if (c == ')') {
      return close();
    }
    if (not is_licence_char(c)) {
      return fail(at_, Errc::bad_character);
    }
    return word();
  }

  constexpr Error open() noexcept {
    if (expect_ != Expect::operand) {
      return fail(at_, expect_ == Expect::exception ? Errc::expected_licence
                                                    : Errc::expected_operator);
    }
    ++depth_;
    ++at_;
    return std::nullopt;
  }

  constexpr Error close() noexcept {
    if (expect_ != Expect::operator_) {
      return fail(at_, Errc::expected_licence);
    }
    if (depth_ == 0) {
      return fail(at_, Errc::unbalanced);
    }
    --depth_;
    after_identifier_ = false;
    ++at_;
    return std::nullopt;
  }

  constexpr Error word() {
    std::size_t end = at_;
    while (end < text_.size() and is_licence_char(text_[end])) {
      ++end;
    }
    const std::string_view w = text_.substr(at_, end - at_);
    const Error error = (w == "AND" or w == "OR") ? conjunction()
                        : w == "WITH"             ? with()
                                                  : identifier(w);
    at_ = end;
    return error;
  }

  constexpr Error conjunction() noexcept {
    if (expect_ != Expect::operator_) {
      return fail(at_, Errc::expected_licence);
    }
    expect_ = Expect::operand;
    after_identifier_ = false;
    return std::nullopt;
  }

  constexpr Error with() noexcept {
    if (expect_ != Expect::operator_) {
      return fail(at_, Errc::expected_licence);
    }
    if (not after_identifier_) {
      return fail(at_, Errc::expected_operator);
    }
    expect_ = Expect::exception;
    after_identifier_ = false;
    return std::nullopt;
  }

  constexpr Error identifier(std::string_view id) {
    if (expect_ == Expect::operator_) {
      return fail(at_, Errc::expected_operator);
    }
    if (const auto bad = bad_licence_ref(id, at_)) {
      return fail(*bad, Errc::bad_character);
    }
    (expect_ == Expect::exception ? exceptions_ : identifiers_)
        .emplace_back(id);
    after_identifier_ = expect_ == Expect::operand;
    expect_ = Expect::operator_;
    return std::nullopt;
  }

  [[nodiscard]] constexpr Error finish() const noexcept {
    if (identifiers_.empty() and expect_ == Expect::operand and depth_ == 0) {
      return fail(0, Errc::empty);
    }
    if (expect_ != Expect::operator_) {
      return fail(text_.size(), Errc::expected_licence);
    }
    if (depth_ != 0) {
      return fail(text_.size(), Errc::unbalanced);
    }
    return std::nullopt;
  }

  std::string_view text_;
  std::size_t at_ = 0;
  std::size_t depth_ = 0;
  Expect expect_ = Expect::operand;
  bool after_identifier_ = false;  // WITH may follow
  std::vector<std::string> identifiers_;
  std::vector<std::string> exceptions_;
};

}  // namespace mov::core::detail

namespace mov::core::tide {

/// A parsed licence expression: its text, kept verbatim, and its tokens.
/// Regular; there is no default (an absent licence is std::optional).
class Licence {
 public:
  /// Parses `expr` against the grammar
  ///   expression := term { ("AND" | "OR") term }
  ///   term       := identifier [ "WITH" identifier ] | "(" expression ")"
  /// with identifiers [A-Za-z0-9.+-]+ ("LicenseRef-" then [A-Za-z0-9.-]+),
  /// operators in upper case only, and spaces between tokens. Precedence is
  /// not needed for what the format asks of an expression (its identifiers),
  /// so it is not represented. Linear and without recursion: nesting depth
  /// is a counter.
  [[nodiscard]] static constexpr std::expected<Licence, LicenceError> parse(
      std::string_view expr) {
    detail::LicenceParser parser{expr};
    if (const auto error = parser.run()) {
      return std::unexpected(*error);
    }
    return Licence{std::string{expr}, std::move(parser).identifiers(),
                   std::move(parser).exceptions()};
  }

  /// The expression as written.
  [[nodiscard]] constexpr std::string_view text() const& noexcept {
    return text_;
  }
  std::string_view text() const&& = delete;  // would dangle

  /// The licence identifiers, in order of appearance (never empty).
  [[nodiscard]] constexpr std::span<const std::string> identifiers()
      const& noexcept {
    return identifiers_;
  }
  std::span<const std::string> identifiers() const&& = delete;

  /// The exception identifiers after WITH, in order of appearance.
  [[nodiscard]] constexpr std::span<const std::string> exceptions()
      const& noexcept {
    return exceptions_;
  }
  std::span<const std::string> exceptions() const&& = delete;

  /// Whether any identifier, licence or exception, has a "-NC" component
  /// (CC-BY-NC-4.0, CC-BY-NC-SA-4.0), compared ignoring case so that an
  /// allow-list that refuses non-commercial terms fails closed.
  [[nodiscard]] constexpr bool carries_non_commercial() const noexcept {
    const auto any_nc = [](const std::vector<std::string>& ids) {
      for (const std::string& id : ids) {
        if (detail::has_nc_component(id)) {
          return true;
        }
      }
      return false;
    };
    return any_nc(identifiers_) or any_nc(exceptions_);
  }

  friend constexpr bool operator==(const Licence&, const Licence&) = default;

 private:
  constexpr Licence(std::string text, std::vector<std::string> identifiers,
                    std::vector<std::string> exceptions)
      : text_{std::move(text)},
        identifiers_{std::move(identifiers)},
        exceptions_{std::move(exceptions)} {}

  std::string text_;
  std::vector<std::string> identifiers_;  // derived from text_
  std::vector<std::string> exceptions_;   // derived from text_
};

}  // namespace mov::core::tide
