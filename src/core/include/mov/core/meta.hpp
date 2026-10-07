// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "mov/core/datum.hpp"
#include "mov/core/detail/core_key.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/units.hpp"

namespace mov::core {

enum class AssumeUnitError : std::uint8_t { already_set };
enum class AssumeDatumError : std::uint8_t { already_set, not_applicable };

/// What a series measures, how it is labelled, its unit and its vertical
/// datum. Invariant (C3): a datum is engaged only if
/// datum_applicable(quantity()). make cannot set a datum, so it is total;
/// assume_datum is the one checked way to add one. An engaged unit or datum
/// is never replaced here: it changes only through convert() and shift(),
/// which also change the values (rewrite_unit / rewrite_datum, core only).
class SeriesMeta {
 public:
  /// The input of make, for designated initializers. Every field may be
  /// left out: the quantity defaults to the generic `value`.
  struct Fields {
    QuantityId quantity{};
    std::string label{};
    std::optional<Unit> unit{};
    friend bool operator==(const Fields&, const Fields&) = default;
  };

  /// The generic `value` quantity, an empty label, no unit and no datum.
  SeriesMeta() = default;

  /// Total: every Fields value is valid metadata (without a datum).
  [[nodiscard]] static SeriesMeta make(Fields f) {
    return SeriesMeta{std::move(f)};
  }

  [[nodiscard]] const QuantityId& quantity() const& noexcept {
    return f_.quantity;
  }
  const QuantityId& quantity() const&& = delete;
  [[nodiscard]] std::string_view label() const& noexcept { return f_.label; }
  std::string_view label() const&& = delete;
  [[nodiscard]] const std::optional<Unit>& unit() const& noexcept {
    return f_.unit;
  }
  const std::optional<Unit>& unit() const&& = delete;
  [[nodiscard]] std::optional<VerticalDatum> datum() const noexcept {
    return datum_;
  }

  /// The same metadata with another label. Total.
  [[nodiscard]] SeriesMeta with_label(std::string label) const&;
  [[nodiscard]] SeriesMeta with_label(std::string label) &&;

  /// Sets the unit if it is unset; already_set otherwise (even to the same
  /// unit: an engaged unit changes only through convert).
  [[nodiscard]] std::expected<SeriesMeta, AssumeUnitError> assume_unit(
      Unit u) const;

  /// Sets the datum if it is unset (else already_set) and the quantity can
  /// carry one (else not_applicable).
  [[nodiscard]] std::expected<SeriesMeta, AssumeDatumError> assume_datum(
      VerticalDatum d) const;

  /// For convert(): the metadata after the values were converted to `to`.
  [[nodiscard]] SeriesMeta rewrite_unit(const detail::CoreKey& /*key*/,
                                        Unit to) const {
    SeriesMeta copy = *this;
    copy.f_.unit = std::move(to);
    return copy;
  }

  /// For shift(): the metadata after the values were shifted to `to`.
  /// Replaces an engaged datum only (which implies datum_applicable);
  /// nullopt if none is engaged.
  [[nodiscard]] std::optional<SeriesMeta> rewrite_datum(
      const detail::CoreKey& /*key*/, VerticalDatum to) const {
    if (not datum_) {
      return std::nullopt;
    }
    SeriesMeta copy = *this;
    copy.datum_ = to;
    return copy;
  }

  friend bool operator==(const SeriesMeta&, const SeriesMeta&) = default;

 private:
  explicit SeriesMeta(Fields f) : f_{std::move(f)} {}

  Fields f_;
  std::optional<VerticalDatum> datum_;
};

}  // namespace mov::core
