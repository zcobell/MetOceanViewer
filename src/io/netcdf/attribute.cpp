// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// File and NewFile: reading and writing attributes.

#include <netcdf.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "internal.hpp"
#include "mov/io/netcdf/file.hpp"
#include "nc_call.hpp"

namespace mov::io::nc {

namespace {

// Typed attribute access. int64_t is long on LP64 Linux and long long on
// Windows and macOS; netCDF-C takes long long, so a long buffer is copied.
template <class I>
int get_att_int64(int ncid, int varid, const char* name, I* out,
                  std::size_t n) {
  if constexpr (std::same_as<I, long long>) {
    return nc_get_att_longlong(ncid, varid, name, out);
  } else {
    std::vector<long long> buffer(n);
    const int status = nc_get_att_longlong(ncid, varid, name, buffer.data());
    std::ranges::copy(buffer, out);
    return status;
  }
}

template <class I>
int put_att_int64(int ncid, int varid, const char* name, std::span<const I> v) {
  if constexpr (std::same_as<I, long long>) {
    return nc_put_att_longlong(ncid, varid, name, NC_INT64, v.size(), v.data());
  } else {
    const std::vector<long long> buffer(v.begin(), v.end());
    return nc_put_att_longlong(ncid, varid, name, NC_INT64, buffer.size(),
                               buffer.data());
  }
}

template <Numeric T>
int get_att_values(int ncid, int varid, const char* name, std::span<T> out) {
  if constexpr (std::same_as<T, double>) {
    return nc_get_att_double(ncid, varid, name, out.data());
  } else if constexpr (std::same_as<T, float>) {
    return nc_get_att_float(ncid, varid, name, out.data());
  } else if constexpr (std::same_as<T, std::int8_t>) {
    return nc_get_att_schar(ncid, varid, name, out.data());
  } else if constexpr (std::same_as<T, std::int16_t>) {
    return nc_get_att_short(ncid, varid, name, out.data());
  } else if constexpr (std::same_as<T, std::int32_t>) {
    return nc_get_att_int(ncid, varid, name, out.data());
  } else {
    return get_att_int64(ncid, varid, name, out.data(), out.size());
  }
}

template <Numeric T>
int put_att_values(int ncid, int varid, const char* name,
                   std::span<const T> v) {
  const int xtype = detail::to_nc_type(type_of<T>);
  if constexpr (std::same_as<T, double>) {
    return nc_put_att_double(ncid, varid, name, xtype, v.size(), v.data());
  } else if constexpr (std::same_as<T, float>) {
    return nc_put_att_float(ncid, varid, name, xtype, v.size(), v.data());
  } else if constexpr (std::same_as<T, std::int8_t>) {
    return nc_put_att_schar(ncid, varid, name, xtype, v.size(), v.data());
  } else if constexpr (std::same_as<T, std::int16_t>) {
    return nc_put_att_short(ncid, varid, name, xtype, v.size(), v.data());
  } else if constexpr (std::same_as<T, std::int32_t>) {
    return nc_put_att_int(ncid, varid, name, xtype, v.size(), v.data());
  } else {
    return put_att_int64(ncid, varid, name, v);
  }
}

// The strings nc_get_att_string allocated, freed through the choke point
// whatever happens to the copy.
class AttStrings {
 public:
  explicit AttStrings(std::size_t n) : ptrs_(n, nullptr) {}
  AttStrings(const AttStrings&) = delete;
  AttStrings& operator=(const AttStrings&) = delete;
  AttStrings(AttStrings&&) = delete;
  AttStrings& operator=(AttStrings&&) = delete;
  ~AttStrings() {
    static_cast<void>(detail::nc_status(
        [this] { return nc_free_string(ptrs_.size(), ptrs_.data()); }));
  }
  [[nodiscard]] char** data() noexcept { return ptrs_.data(); }
  [[nodiscard]] const char* at(std::size_t i) const { return ptrs_.at(i); }

 private:
  std::vector<char*> ptrs_;
};

}  // namespace

std::expected<std::optional<Type>, NcError> File::att_type(
    AttTarget on, NcNameRef name) const {
  const auto owner = att_owner(on, name, NcOp::get_att);
  if (not owner) {
    return std::unexpected{owner.error()};
  }
  return att_shape(*owner, on, name).transform([](const auto& shape) {
    return shape.transform(
        [](const AttShape& found) -> Type { return found.type; });
  });
}

std::expected<std::optional<std::string>, NcError> File::text_att(
    AttTarget on, NcNameRef name) const {
  using Result = std::expected<std::optional<std::string>, NcError>;
  const auto owner = att_owner(on, name, NcOp::get_att);
  if (not owner) {
    return std::unexpected{owner.error()};
  }
  const auto refuse = [&](WrapperFault fault) -> Result {
    return std::unexpected{att_fail(fault, NcOp::get_att, on, name)};
  };
  const auto read_chars = [&](std::size_t length) -> Result {
    if (length > limits_.max_att_bytes) {
      return refuse(WrapperFault::too_large);
    }
    std::string text(length, '\0');
    return att_status(detail::nc_status([&] {
                        return nc_get_att_text(*ncid_, *owner, name.c_str(),
                                               text.data());
                      }),
                      NcOp::get_att, on, name)
        .transform([&] { return std::optional{std::move(text)}; });
  };
  const auto read_string = [&]() -> Result {
    AttStrings strings{1};
    if (auto done = att_status(detail::nc_status([&] {
                                 return nc_get_att_string(*ncid_, *owner,
                                                          name.c_str(),
                                                          strings.data());
                               }),
                               NcOp::get_att, on, name);
        not done) {
      return std::unexpected{done.error()};
    }
    const char* raw = strings.at(0);
    const std::string_view value =
        raw == nullptr ? std::string_view{} : std::string_view{raw};
    if (value.size() > limits_.max_att_bytes) {
      return refuse(WrapperFault::too_large);
    }
    return std::optional{std::string{value}};
  };
  return att_shape(*owner, on, name)
      .and_then([&](const std::optional<AttShape>& shape) -> Result {
        if (not shape) {
          return std::nullopt;
        }
        if (shape->type == Type::char_) {
          return read_chars(shape->length);
        }
        if (shape->type != Type::string) {
          return refuse(WrapperFault::type_mismatch);
        }
        return shape->length == 1 ? read_string()
                                  : refuse(WrapperFault::count_mismatch);
      });
}

std::expected<std::optional<File::AttPlan>, NcError> File::plan_numeric_att(
    AttTarget on, NcNameRef name, Type type, std::size_t element_size) const {
  using Result = std::expected<std::optional<AttPlan>, NcError>;
  const auto owner = att_owner(on, name, NcOp::get_att);
  if (not owner) {
    return std::unexpected{owner.error()};
  }
  return att_shape(*owner, on, name)
      .and_then([&](const std::optional<AttShape>& shape) -> Result {
        if (not shape) {
          return std::nullopt;
        }
        if (shape->type != type) {
          return std::unexpected{
              att_fail(WrapperFault::type_mismatch, NcOp::get_att, on, name)};
        }
        if (shape->length > limits_.max_att_bytes / element_size) {
          return std::unexpected{
              att_fail(WrapperFault::too_large, NcOp::get_att, on, name)};
        }
        return AttPlan{.owner = *owner, .length = shape->length};
      });
}

template <Numeric T>
std::expected<std::optional<std::vector<T>>, NcError> File::numeric_att(
    AttTarget on, NcNameRef name) const {
  using Result = std::expected<std::optional<std::vector<T>>, NcError>;
  return plan_numeric_att(on, name, type_of<T>, sizeof(T))
      .and_then([&](const std::optional<AttPlan>& plan) -> Result {
        // Absent stays absent; an empty attribute needs no read.
        std::vector<T> values(plan ? plan->length : 0);
        if (values.empty()) {
          return plan.transform(
              [](const AttPlan&) { return std::vector<T>{}; });
        }
        return att_status(detail::nc_status([&] {
                            return get_att_values<T>(*ncid_, plan->owner,
                                                     name.c_str(),
                                                     std::span{values});
                          }),
                          NcOp::get_att, on, name)
            .transform([&] { return std::optional{std::move(values)}; });
      });
}

std::expected<int, NcError> NewFile::plan_put_att(AttTarget on, NcNameRef name,
                                                  std::size_t bytes) const {
  if (bytes > limits_.max_att_bytes) {
    return std::unexpected{
        att_fail(WrapperFault::too_large, NcOp::put_att, on, name)};
  }
  return att_owner(on, name, NcOp::put_att);
}

std::expected<void, NcError> NewFile::put_att(AttTarget on, NcNameRef name,
                                              std::string_view text) {
  return plan_put_att(on, name, text.size()).and_then([&](int owner) {
    return att_status(detail::nc_status([&] {
                        return nc_put_att_text(*ncid_, owner, name.c_str(),
                                               text.size(), text.data());
                      }),
                      NcOp::put_att, on, name);
  });
}

template <Numeric T>
std::expected<void, NcError> NewFile::put_att(AttTarget on, NcNameRef name,
                                              std::span<const T> values) {
  // A count over the limit in elements is over it in bytes too, without
  // overflowing the product.
  const std::size_t bytes = values.size() > limits_.max_att_bytes / sizeof(T)
                                ? limits_.max_att_bytes + 1
                                : values.size() * sizeof(T);
  return plan_put_att(on, name, bytes).and_then([&](int owner) {
    return att_status(detail::nc_status([&] {
                        return put_att_values<T>(*ncid_, owner, name.c_str(),
                                                 values);
                      }),
                      NcOp::put_att, on, name);
  });
}

template std::expected<std::optional<std::vector<double>>, NcError>
    File::numeric_att<double>(AttTarget, NcNameRef) const;
template std::expected<void, NcError> NewFile::put_att<double>(
    AttTarget, NcNameRef, std::span<const double>);
template std::expected<std::optional<std::vector<float>>, NcError>
    File::numeric_att<float>(AttTarget, NcNameRef) const;
template std::expected<void, NcError> NewFile::put_att<float>(
    AttTarget, NcNameRef, std::span<const float>);
template std::expected<std::optional<std::vector<std::int8_t>>, NcError>
    File::numeric_att<std::int8_t>(AttTarget, NcNameRef) const;
template std::expected<void, NcError> NewFile::put_att<std::int8_t>(
    AttTarget, NcNameRef, std::span<const std::int8_t>);
template std::expected<std::optional<std::vector<std::int16_t>>, NcError>
    File::numeric_att<std::int16_t>(AttTarget, NcNameRef) const;
template std::expected<void, NcError> NewFile::put_att<std::int16_t>(
    AttTarget, NcNameRef, std::span<const std::int16_t>);
template std::expected<std::optional<std::vector<std::int32_t>>, NcError>
    File::numeric_att<std::int32_t>(AttTarget, NcNameRef) const;
template std::expected<void, NcError> NewFile::put_att<std::int32_t>(
    AttTarget, NcNameRef, std::span<const std::int32_t>);
template std::expected<std::optional<std::vector<std::int64_t>>, NcError>
    File::numeric_att<std::int64_t>(AttTarget, NcNameRef) const;
template std::expected<void, NcError> NewFile::put_att<std::int64_t>(
    AttTarget, NcNameRef, std::span<const std::int64_t>);

}  // namespace mov::io::nc
