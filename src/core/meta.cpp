// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/meta.hpp"

#include <expected>
#include <string>
#include <utility>

namespace mov::core {

std::expected<SeriesMeta, MetaError> SeriesMeta::make(Fields f) {
  if (f.datum and not datum_applicable(f.quantity)) {
    return std::unexpected{MetaError::datum_not_applicable};
  }
  return SeriesMeta{std::move(f)};
}

SeriesMeta SeriesMeta::with_label(std::string label) const& {
  SeriesMeta copy = *this;
  copy.f_.label = std::move(label);
  return copy;
}

SeriesMeta SeriesMeta::with_label(std::string label) && {
  f_.label = std::move(label);
  return std::move(*this);
}

std::expected<SeriesMeta, MetaError> SeriesMeta::assume_unit(Unit u) const {
  if (f_.unit) {
    return std::unexpected{MetaError::already_set};
  }
  SeriesMeta copy = *this;
  copy.f_.unit = std::move(u);
  return copy;
}

std::expected<SeriesMeta, MetaError> SeriesMeta::assume_datum(
    VerticalDatum d) const {
  if (f_.datum) {
    return std::unexpected{MetaError::already_set};
  }
  return with_datum(d);
}

std::expected<SeriesMeta, MetaError> SeriesMeta::with_datum(
    VerticalDatum to) const {
  if (not datum_applicable(f_.quantity)) {
    return std::unexpected{MetaError::datum_not_applicable};
  }
  SeriesMeta copy = *this;
  copy.f_.datum = to;
  return copy;
}

}  // namespace mov::core
