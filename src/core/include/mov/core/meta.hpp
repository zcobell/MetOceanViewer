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
#include "mov/core/quantity.hpp"
#include "mov/core/units.hpp"

namespace mov::core {

enum class MetaError : std::uint8_t { datum_not_applicable, already_set };

namespace detail {

class MetaRewrite;
struct SeriesOpsAccess;
struct DatumShiftAccess;

/// Passkey for the SeriesMeta members that replace an engaged unit or datum.
/// Only convert() (series_ops.cpp, through SeriesOpsAccess) and shift()
/// (datum_shift.cpp, through DatumShiftAccess) can construct one (C3).
class MetaRewrite {
 private:
  friend struct SeriesOpsAccess;
  friend struct DatumShiftAccess;
  MetaRewrite() noexcept = default;
};

}  // namespace detail

/// What a series measures, how it is labelled, its unit and its vertical
/// datum. The class invariant (C3): a datum is engaged only if
/// datum_applicable(quantity()). An engaged unit or datum is never replaced
/// here; it changes only through convert() and shift(), which also change the
/// values. assume_unit and assume_datum fill a field that is still unset.
class SeriesMeta {
 public:
  /// The input of make, for designated initializers. Every field may be
  /// left out: the quantity defaults to the generic `value`.
  struct Fields {
    QuantityId quantity{};
    std::string label{};
    std::optional<Unit> unit{};
    std::optional<VerticalDatum> datum{};
    friend bool operator==(const Fields&, const Fields&) = default;
  };

  /// The generic `value` quantity, an empty label, no unit and no datum.
  SeriesMeta() = default;

  /// datum_not_applicable if f.datum is engaged but the quantity cannot carry
  /// one.
  [[nodiscard]] static std::expected<SeriesMeta, MetaError> make(Fields f);

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
    return f_.datum;
  }

  /// The same metadata with another label. Total.
  [[nodiscard]] SeriesMeta with_label(std::string label) const&;
  [[nodiscard]] SeriesMeta with_label(std::string label) &&;

  /// Sets the unit if it is unset; already_set otherwise (even to the same
  /// unit: an engaged unit changes only through convert).
  [[nodiscard]] std::expected<SeriesMeta, MetaError> assume_unit(Unit u) const;

  /// Sets the datum if it is unset and the quantity can carry one:
  /// already_set, else datum_not_applicable.
  [[nodiscard]] std::expected<SeriesMeta, MetaError> assume_datum(
      VerticalDatum d) const;

  /// For convert(): the metadata after the values were converted to `to`.
  [[nodiscard]] SeriesMeta rewrite_unit(const detail::MetaRewrite& /*key*/,
                                        Unit to) const {
    SeriesMeta copy = *this;
    copy.f_.unit = std::move(to);
    return copy;
  }

  /// For shift(): the metadata after the values were shifted to `to`.
  /// datum_not_applicable if the quantity cannot carry a datum.
  [[nodiscard]] std::expected<SeriesMeta, MetaError> rewrite_datum(
      const detail::MetaRewrite& /*key*/, VerticalDatum to) const {
    return with_datum(to);
  }

  friend bool operator==(const SeriesMeta&, const SeriesMeta&) = default;

 private:
  explicit SeriesMeta(Fields f) : f_{std::move(f)} {}

  // The datum replaced; datum_not_applicable if the quantity has none.
  [[nodiscard]] std::expected<SeriesMeta, MetaError> with_datum(
      VerticalDatum to) const;

  Fields f_;
};

}  // namespace mov::core
