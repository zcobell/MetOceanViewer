// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// File::masking: the CF missing-data attributes of a variable, in its own
// type (B4, B9; SN section 8.1).

#include <netcdf.h>

#include <concepts>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/io/detail/text.hpp"
#include "mov/io/netcdf/file.hpp"
#include "nc_call.hpp"

namespace mov::io::nc {

namespace {

NcError count_mismatch(const File& file, NcNameRef var, NcNameRef att) {
  std::string object{var.view()};
  object += ':';
  object += att.view();
  return NcError{.status = WrapperFault::count_mismatch,
                 .op = NcOp::get_att,
                 .object = std::move(object),
                 .file = file.path()};
}

// The attribute's values if it has exactly `n` of them (n of 0: any number).
template <Numeric T>
std::expected<std::optional<std::vector<T>>, NcError> values_of(
    const File& file, NcNameRef var, NcNameRef att, std::size_t n) {
  return file.numeric_att<T>(var, att).and_then(
      [&](std::optional<std::vector<T>> values)
          -> std::expected<std::optional<std::vector<T>>, NcError> {
        if (values and n != 0 and values->size() != n) {
          return std::unexpected{count_mismatch(file, var, att)};
        }
        return std::move(values);
      });
}

// A one-element attribute.
template <Numeric T>
std::expected<std::optional<T>, NcError> one_of(const File& file, NcNameRef var,
                                                NcNameRef att) {
  return values_of<T>(file, var, att, 1)
      .transform(
          [](const std::optional<std::vector<T>>& values) -> std::optional<T> {
            return values ? std::optional{values->front()} : std::nullopt;
          });
}

// scale_factor or add_offset: one float or double, as a double.
std::expected<std::optional<double>, NcError> packing_att(const File& file,
                                                          NcNameRef var,
                                                          NcNameRef att) {
  auto as_double = one_of<double>(file, var, att);
  if (as_double or
      as_double.error().status != NcStatus{WrapperFault::type_mismatch}) {
    return as_double;
  }
  return one_of<float>(file, var, att).transform([](std::optional<float> v) {
    return v.transform([](float f) { return static_cast<double>(f); });
  });
}

// `_Unsigned = "true"` (any case, NUL-padded or not).
std::expected<bool, NcError> is_unsigned(const File& file, NcNameRef var) {
  return file.text_att(var, "_Unsigned")
      .transform([](const std::optional<std::string>& text) {
        return text and io::detail::to_lower_ascii(io::detail::simplified(
                            io::detail::cut_at_nul(*text))) == "true";
      });
}

// The valid range: valid_range when present, else valid_min and valid_max.
template <Numeric T>
std::expected<Masking<T>, NcError> with_valid_range(const File& file,
                                                    NcNameRef var,
                                                    Masking<T> mask) {
  using Result = std::expected<Masking<T>, NcError>;
  return values_of<T>(file, var, "valid_range", 2)
      .and_then([&](const std::optional<std::vector<T>>& range) -> Result {
        if (range) {
          mask.valid_min = range->front();
          mask.valid_max = range->back();
          return mask;
        }
        return one_of<T>(file, var, "valid_min")
            .and_then([&](std::optional<T> min) {
              mask.valid_min = min;
              return one_of<T>(file, var, "valid_max");
            })
            .transform([&](std::optional<T> max) {
              mask.valid_max = max;
              return mask;
            });
      });
}

struct Packing {
  std::optional<double> scale;
  std::optional<double> offset;
};

std::expected<Packing, NcError> packing_of(const File& file, NcNameRef var) {
  return packing_att(file, var, "scale_factor")
      .and_then([&](std::optional<double> scale) {
        return packing_att(file, var, "add_offset")
            .transform([&](std::optional<double> offset) {
              return Packing{.scale = scale, .offset = offset};
            });
      });
}

}  // namespace

template <Numeric T>
std::expected<std::optional<T>, NcError> File::default_fill(
    int varid, NcNameRef var) const {
  using Result = std::expected<std::optional<T>, NcError>;
  if constexpr (std::same_as<T, std::int8_t>) {
    return Result{std::nullopt};
  } else {
    return id(NcOp::get_att, var.view()).and_then([&](int ncid) -> Result {
      int no_fill = 0;
      T fill{};
      return detail::nc_call(
                 NcOp::get_att, var.view(), path_,
                 [&] { return nc_inq_var_fill(ncid, varid, &no_fill, &fill); })
          .transform([&] {
            return no_fill == 0 ? std::optional{fill} : std::nullopt;
          });
    });
  }
}

std::expected<int, NcError> File::plan_masking(NcNameRef name,
                                               Type type) const {
  const auto info = var(name, NcOp::get_att);
  if (not info) {
    return std::unexpected{info.error()};
  }
  if (info->type != type) {
    return std::unexpected{
        fail(WrapperFault::type_mismatch, NcOp::get_att, name.view())};
  }
  const auto is = is_unsigned(*this, name);
  if (not is) {
    return std::unexpected{is.error()};
  }
  if (*is) {
    return std::unexpected{
        fail(WrapperFault::unsupported_unsigned, NcOp::get_att, name.view())};
  }
  return info->id;
}

template <Numeric T>
std::expected<Masking<T>, NcError> File::masking(NcNameRef name) const {
  using Fill = std::expected<std::optional<T>, NcError>;
  // The _FillValue attribute is read with its type checked, never through
  // nc_inq_var_fill, which copies an attribute of another type into a buffer
  // sized for the variable's (B4).
  const auto fill = [&](int varid) {
    return one_of<T>(*this, name, "_FillValue")
        .and_then([&](std::optional<T> value) -> Fill {
          return value ? Fill{value} : default_fill<T>(varid, name);
        });
  };
  const auto with_missing = [&](std::optional<T> fill_value) {
    return values_of<T>(*this, name, "missing_value", 0)
        .transform([&](std::optional<std::vector<T>> missing) {
          return Masking<T>{
              .fill = fill_value,
              .missing_values = std::move(missing).value_or(std::vector<T>{})};
        });
  };
  return plan_masking(name, type_of<T>)
      .and_then(fill)
      .and_then(with_missing)
      .and_then([&](Masking<T> mask) {
        return with_valid_range(*this, name, std::move(mask));
      })
      .and_then([&](Masking<T> mask) {
        return packing_of(*this, name).transform([&](const Packing& packing) {
          mask.scale = packing.scale;
          mask.offset = packing.offset;
          return std::move(mask);
        });
      });
}

template std::expected<Masking<double>, NcError> File::masking<double>(
    NcNameRef) const;
template std::expected<Masking<float>, NcError> File::masking<float>(
    NcNameRef) const;
template std::expected<Masking<std::int8_t>, NcError>
    File::masking<std::int8_t>(NcNameRef) const;
template std::expected<Masking<std::int16_t>, NcError>
    File::masking<std::int16_t>(NcNameRef) const;
template std::expected<Masking<std::int32_t>, NcError>
    File::masking<std::int32_t>(NcNameRef) const;
template std::expected<Masking<std::int64_t>, NcError>
    File::masking<std::int64_t>(NcNameRef) const;

}  // namespace mov::io::nc
