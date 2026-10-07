// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// File::masking: the CF missing-data attributes of a variable, in its own
// type (B4, B9; SN section 8.1). plan_masking fetches and checks every
// attribute once, whatever the variable's type; masking<T> only converts
// the values to T, exactly or not at all.

#include <netcdf.h>

#include <concepts>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "mov/io/detail/text.hpp"
#include "mov/io/netcdf/file.hpp"
#include "nc_call.hpp"

namespace mov::io::nc {

namespace {

// Every value of `from` as a T, or nullopt if one is not exact.
template <Numeric T, class S>
std::optional<std::vector<T>> exact_values(const std::vector<S>& from) {
  std::vector<T> out;
  out.reserve(from.size());
  for (const S v : from) {
    const std::optional<T> t = detail::exact_from<T>(v);
    if (not t) {
      return std::nullopt;
    }
    out.push_back(*t);
  }
  return out;
}

template <Numeric T>
std::optional<std::vector<T>> exact_all(const detail::AttNumbers& numbers) {
  return std::visit([](const auto& v) { return exact_values<T>(v); }, numbers);
}

// `_Unsigned = "true"` (any case, NUL-padded or not).
bool says_true(const std::optional<std::string>& text) {
  return text and io::detail::to_lower_ascii(io::detail::simplified(
                      io::detail::cut_at_nul(*text))) == "true";
}

}  // namespace

std::expected<std::optional<File::AttNumbers>, NcError> File::att_numbers(
    int varid, NcNameRef var, NcNameRef att, std::size_t count) const {
  using Result = std::expected<std::optional<AttNumbers>, NcError>;
  const auto refuse = [&](WrapperFault fault) -> Result {
    return std::unexpected{att_fail(fault, NcOp::get_att, var, att)};
  };
  const auto shape = att_shape(varid, var, att);
  if (not shape) {
    return std::unexpected{shape.error()};
  }
  if (not *shape) {
    return std::nullopt;
  }
  const auto [type, length] = **shape;
  if (count != 0 and length != count) {
    return refuse(WrapperFault::count_mismatch);
  }
  if (length > limits_.max_att_bytes / sizeof(double)) {
    return refuse(WrapperFault::too_large);
  }
  const auto fetch = [&](const auto& get) {
    return att_status(detail::nc_status(get), NcOp::get_att, var, att);
  };
  if (type == Type::float_ or type == Type::double_) {
    std::vector<double> values(length);
    return fetch([&] {
             return nc_get_att_double(*ncid_, varid, att.c_str(),
                                      values.data());
           })
        .transform([&] {
          return std::optional<AttNumbers>{AttNumbers{std::move(values)}};
        });
  }
  if (type == Type::byte or type == Type::short_ or type == Type::int_ or
      type == Type::int64) {
    std::vector<long long> values(length);
    return fetch([&] {
             return nc_get_att_longlong(*ncid_, varid, att.c_str(),
                                        values.data());
           })
        .transform([&] {
          return std::optional<AttNumbers>{AttNumbers{
              std::vector<std::int64_t>(values.begin(), values.end())}};
        });
  }
  return refuse(WrapperFault::type_mismatch);
}

std::expected<std::optional<double>, NcError> File::att_double(
    int varid, NcNameRef var, NcNameRef att) const {
  using Result = std::expected<std::optional<double>, NcError>;
  return att_numbers(varid, var, att, 1)
      .and_then([&](const std::optional<AttNumbers>& numbers) -> Result {
        if (not numbers) {
          return std::nullopt;
        }
        const std::optional<std::vector<double>> value =
            exact_all<double>(*numbers);
        if (not value) {
          return std::unexpected{
              att_fail(WrapperFault::type_mismatch, NcOp::get_att, var, att)};
        }
        return value->front();
      });
}

std::expected<File::MaskingPlan, NcError> File::plan_masking(NcNameRef name,
                                                             Type type) const {
  const auto info = var(name, NcOp::get_att);
  if (not info) {
    return std::unexpected{info.error()};
  }
  const auto refuse = [&](WrapperFault fault) {
    return std::unexpected{fail(fault, NcOp::get_att, name.view())};
  };
  if (info->type != type) {
    return refuse(WrapperFault::type_mismatch);
  }
  const auto is_unsigned = text_att(name, "_Unsigned").transform(says_true);
  if (not is_unsigned) {
    return std::unexpected{is_unsigned.error()};
  }
  if (*is_unsigned) {
    return refuse(WrapperFault::unsupported_unsigned);
  }
  // The _FillValue is strict: the variable's own type and one value. It is
  // read with its type checked, never through nc_inq_var_fill, which copies
  // an attribute of another type into a buffer sized for the variable's (B4).
  const auto fill = att_shape(info->id, name, "_FillValue");
  if (not fill) {
    return std::unexpected{fill.error()};
  }
  if (*fill and (*fill)->type != type) {
    return std::unexpected{att_fail(WrapperFault::type_mismatch, NcOp::get_att,
                                    name, "_FillValue")};
  }
  if (*fill and (*fill)->length != 1) {
    return std::unexpected{att_fail(WrapperFault::count_mismatch, NcOp::get_att,
                                    name, "_FillValue")};
  }
  MaskingPlan plan{.varid = info->id, .has_fill = fill->has_value()};
  const int id = info->id;
  return att_numbers(id, name, "missing_value", 0)
      .and_then([&](std::optional<AttNumbers> missing) {
        plan.missing = std::move(missing);
        return att_numbers(id, name, "valid_range", 2);
      })
      .and_then([&](std::optional<AttNumbers> range) {
        plan.range = std::move(range);
        return att_numbers(id, name, "valid_min", 1);
      })
      .and_then([&](std::optional<AttNumbers> min) {
        plan.min = std::move(min);
        return att_numbers(id, name, "valid_max", 1);
      })
      .and_then([&](std::optional<AttNumbers> max) {
        plan.max = std::move(max);
        return att_double(id, name, "scale_factor");
      })
      .and_then([&](std::optional<double> scale) {
        plan.scale = scale;
        return att_double(id, name, "add_offset");
      })
      .transform([&](std::optional<double> offset) {
        plan.offset = offset;
        return std::move(plan);
      });
}

template <Numeric T>
std::expected<std::optional<T>, NcError> File::default_fill(
    int varid, NcNameRef var) const {
  using Result = std::expected<std::optional<T>, NcError>;
  if constexpr (std::same_as<T, std::int8_t>) {
    return Result{std::nullopt};
  } else {
    int no_fill = 0;
    T fill{};
    return detail::nc_call(
               NcOp::get_att, var.view(), path_,
               [&] { return nc_inq_var_fill(*ncid_, varid, &no_fill, &fill); })
        .transform(
            [&] { return no_fill == 0 ? std::optional{fill} : std::nullopt; });
  }
}

template <Numeric T>
std::expected<Masking<T>, NcError> File::masking(NcNameRef name) const {
  using Values = std::expected<std::optional<std::vector<T>>, NcError>;
  const auto plan = plan_masking(name, type_of<T>);
  if (not plan) {
    return std::unexpected{plan.error()};
  }
  const auto fill =
      plan->has_fill
          ? numeric_att<T>(name, "_FillValue")
                .transform([](const std::optional<std::vector<T>>& v) {
                  return v.transform(
                      [](const std::vector<T>& one) { return one.front(); });
                })
          : default_fill<T>(plan->varid, name);
  if (not fill) {
    return std::unexpected{fill.error()};
  }
  // An attribute present but not exact in T is a type mismatch.
  const auto exact = [&](const std::optional<AttNumbers>& numbers,
                         NcNameRef att) -> Values {
    if (not numbers) {
      return std::nullopt;
    }
    std::optional<std::vector<T>> values = exact_all<T>(*numbers);
    if (not values) {
      return std::unexpected{
          att_fail(WrapperFault::type_mismatch, NcOp::get_att, name, att)};
    }
    return values;
  };
  Masking<T> mask{.fill = *fill, .scale = plan->scale, .offset = plan->offset};
  return exact(plan->missing, "missing_value")
      .and_then([&](std::optional<std::vector<T>> missing) {
        mask.missing_values = std::move(missing).value_or(std::vector<T>{});
        return exact(plan->range, "valid_range");
      })
      .and_then([&](const std::optional<std::vector<T>>& range) {
        if (range) {  // valid_range wins over valid_min and valid_max
          mask.valid_min = range->front();
          mask.valid_max = range->back();
        }
        return exact(range ? std::nullopt : plan->min, "valid_min");
      })
      .and_then([&](const std::optional<std::vector<T>>& min) {
        if (min) {
          mask.valid_min = min->front();
        }
        return exact(plan->range ? std::nullopt : plan->max, "valid_max");
      })
      .transform([&](const std::optional<std::vector<T>>& max) {
        if (max) {
          mask.valid_max = max->front();
        }
        return std::move(mask);
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
