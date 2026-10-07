// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/meta.hpp"

#include <expected>
#include <string>
#include <utility>

namespace mov::core {

SeriesMeta SeriesMeta::with_label(std::string label) const& {
  SeriesMeta copy = *this;
  copy.f_.label = std::move(label);
  return copy;
}

SeriesMeta SeriesMeta::with_label(std::string label) && {
  f_.label = std::move(label);
  return std::move(*this);
}

std::expected<SeriesMeta, AssumeUnitError> SeriesMeta::assume_unit(
    Unit u) const {
  if (f_.unit) {
    return std::unexpected{AssumeUnitError::already_set};
  }
  SeriesMeta copy = *this;
  copy.f_.unit = std::move(u);
  return copy;
}

std::expected<SeriesMeta, AssumeDatumError> SeriesMeta::assume_datum(
    VerticalDatum d) const {
  if (datum_) {
    return std::unexpected{AssumeDatumError::already_set};
  }
  if (not datum_applicable(f_.quantity)) {
    return std::unexpected{AssumeDatumError::not_applicable};
  }
  SeriesMeta copy = *this;
  copy.datum_ = d;
  return copy;
}

}  // namespace mov::core
