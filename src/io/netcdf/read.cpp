// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// File: bulk data reads.

#include <netcdf.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "internal.hpp"
#include "mov/core/sample.hpp"
#include "mov/io/detail/checked_product.hpp"
#include "mov/io/netcdf/file.hpp"
#include "nc_call.hpp"

namespace mov::io::nc {

namespace detail {

std::expected<std::size_t, NcStatus> check_slab(const VarInfo& var,
                                                const Slab& slab,
                                                const ReadLimits& limits) {
  if (slab.size() != var.dims.size()) {
    return std::unexpected{WrapperFault::rank_mismatch};
  }
  std::vector<std::size_t> counts;
  counts.reserve(slab.size());
  for (std::size_t i = 0; i < slab.size(); ++i) {
    const std::size_t length = var.dims[i].length;
    if (slab[i].start > length) {
      return std::unexpected{LibraryStatus{NC_EINVALCOORDS}};
    }
    if (slab[i].count > length - slab[i].start) {
      return std::unexpected{LibraryStatus{NC_EEDGE}};
    }
    counts.push_back(slab[i].count);
  }
  const std::optional<std::size_t> total = io::detail::checked_product(counts);
  if (not total) {
    return std::unexpected{WrapperFault::overflow};
  }
  if (*total > limits.max_elements) {
    return std::unexpected{WrapperFault::too_large};
  }
  return *total;
}

}  // namespace detail

namespace {

// int64_t is long on LP64 Linux and long long elsewhere; netCDF-C takes long
// long, so a long buffer is read through a copy.
template <class I>
int get_vara_int64(int ncid, int varid, const detail::Block& block, I* out) {
  if constexpr (std::same_as<I, long long>) {
    return nc_get_vara_longlong(ncid, varid, block.start.data(),
                                block.count.data(), out);
  } else {
    std::vector<long long> buffer(block.size);
    const int status = nc_get_vara_longlong(ncid, varid, block.start.data(),
                                            block.count.data(), buffer.data());
    std::ranges::copy(buffer, out);
    return status;
  }
}

// nc_get_vara_<T>, converting from the variable's type (readable_as).
template <Numeric T>
int get_vara(int ncid, int varid, const detail::Block& block, T* out) {
  const std::size_t* start = block.start.data();
  const std::size_t* count = block.count.data();
  if constexpr (std::same_as<T, double>) {
    return nc_get_vara_double(ncid, varid, start, count, out);
  } else if constexpr (std::same_as<T, float>) {
    return nc_get_vara_float(ncid, varid, start, count, out);
  } else if constexpr (std::same_as<T, std::int8_t>) {
    return nc_get_vara_schar(ncid, varid, start, count, out);
  } else if constexpr (std::same_as<T, std::int16_t>) {
    return nc_get_vara_short(ncid, varid, start, count, out);
  } else if constexpr (std::same_as<T, std::int32_t>) {
    return nc_get_vara_int(ncid, varid, start, count, out);
  } else {
    return get_vara_int64(ncid, varid, block, out);
  }
}

// The strings one nc_get_vara_string call allocated, freed through the
// choke point however the copy ends (B21).
class VarStrings {
 public:
  explicit VarStrings(std::size_t n) : ptrs_(n, nullptr) {}
  VarStrings(const VarStrings&) = delete;
  VarStrings& operator=(const VarStrings&) = delete;
  VarStrings(VarStrings&&) = delete;
  VarStrings& operator=(VarStrings&&) = delete;
  ~VarStrings() {
    static_cast<void>(detail::nc_status(
        [this] { return nc_free_string(ptrs_.size(), ptrs_.data()); }));
  }
  [[nodiscard]] char** data() noexcept { return ptrs_.data(); }
  [[nodiscard]] std::span<char* const> strings() const noexcept {
    return ptrs_;
  }

 private:
  std::vector<char*> ptrs_;
};

}  // namespace

std::expected<File::ReadPlan, Error> File::plan_read(
    NcNameRef name, const Slab& slab, const ReadContext& ctx,
    bool (*readable)(Type) noexcept) const {
  const auto info = var(name, NcOp::get_var);
  if (not info) {
    return std::unexpected{Error{info.error()}};
  }
  const auto refuse = [&](NcStatus status) {
    return std::unexpected{Error{fail(status, NcOp::get_var, name.view())}};
  };
  if (not readable(info->type)) {
    return refuse(WrapperFault::type_mismatch);
  }
  const auto total = detail::check_slab(*info, slab, ctx.limits);
  if (not total) {
    return refuse(total.error());
  }
  return ReadPlan{.varid = info->id, .total = *total};
}

template <Numeric T>
std::expected<std::vector<T>, Error> File::read(NcNameRef name,
                                                const Slab& slab,
                                                const ReadContext& ctx) const {
  using Result = std::expected<std::vector<T>, Error>;
  return plan_read(name, slab, ctx, &readable_as<T>)
      .and_then([&](const ReadPlan& plan) -> Result {
        std::vector<T> values(plan.total);
        const std::span<T> out{values};
        return detail::for_each_block(
                   slab, ctx.limits.slab_elements, ctx.stop,
                   [&](const detail::Block& block) {
                     return detail::nc_call(
                                NcOp::get_var, name.view(), path_,
                                [&] {
                                  return get_vara<T>(
                                      *ncid_, plan.varid, block,
                                      out.subspan(block.offset).data());
                                })
                         .transform_error(lift<Error>);
                   })
            .transform([&] { return std::move(values); });
      });
}

std::expected<std::vector<core::Sample>, Error> File::read_samples(
    NcNameRef name, const Slab& slab, const ReadContext& ctx) const {
  using Result = std::expected<std::vector<core::Sample>, Error>;
  const auto info = var(name, NcOp::get_var);
  if (not info) {
    return std::unexpected{Error{info.error()}};
  }
  const auto type_mismatch = [&]() -> Result {
    return std::unexpected{
        Error{fail(WrapperFault::type_mismatch, NcOp::get_var, name.view())}};
  };
  // No 64-bit integer data: a double cannot hold every value (section 4.3).
  if (info->type == Type::int64) {
    return type_mismatch();
  }
  return dispatch_numeric(
      info->type,
      [&]<Numeric T>() -> Result {
        return masking<T>(name)
            .transform_error(lift<Error>)
            .and_then([&](const Masking<T>& mask) {
              return read<T>(name, slab, ctx)
                  .transform([&](const std::vector<T>& raw) {
                    std::vector<core::Sample> samples;
                    samples.reserve(raw.size());
                    std::ranges::transform(raw, std::back_inserter(samples),
                                           [&](T v) { return mask.apply(v); });
                    return samples;
                  });
            });
      },
      type_mismatch);
}

std::expected<std::vector<std::string>, Error> File::read_char_rows(
    NcNameRef name, const ReadContext& ctx) const {
  const auto info = var(name, NcOp::get_var);
  if (not info) {
    return std::unexpected{Error{info.error()}};
  }
  const auto refuse = [&](NcStatus status) {
    return std::unexpected{Error{fail(status, NcOp::get_var, name.view())}};
  };
  if (info->type != Type::char_) {
    return refuse(WrapperFault::type_mismatch);
  }
  if (info->dims.size() != 2) {
    return refuse(WrapperFault::rank_mismatch);
  }
  const Slab slab = whole(*info);
  const auto total = detail::check_slab(*info, slab, ctx.limits);
  if (not total) {
    return refuse(total.error());
  }
  if (info->dims[0].length > ctx.limits.max_elements) {
    return refuse(WrapperFault::too_large);  // rows of a zero-length stride
  }
  const std::size_t stride = info->dims[1].length;
  std::string bytes(*total, '\0');
  const std::span<char> out{bytes};
  auto done = detail::for_each_block(
      slab, ctx.limits.slab_elements, ctx.stop,
      [&](const detail::Block& block) -> std::expected<void, Error> {
        return detail::nc_call(NcOp::get_var, name.view(), path_,
                               [&] {
                                 return nc_get_vara_text(
                                     *ncid_, info->id, block.start.data(),
                                     block.count.data(),
                                     out.subspan(block.offset).data());
                               })
            .transform_error(lift<Error>);
      });
  if (not done) {
    return std::unexpected{done.error()};
  }
  std::vector<std::string> rows;
  rows.reserve(info->dims[0].length);
  const std::string_view all{bytes};
  for (std::size_t row = 0; row < info->dims[0].length; ++row) {
    rows.emplace_back(all.substr(row * stride, stride));
  }
  return rows;
}

std::expected<std::vector<std::string>, Error> File::read_strings(
    NcNameRef name, const ReadContext& ctx) const {
  const auto info = var(name, NcOp::get_var);
  if (not info) {
    return std::unexpected{Error{info.error()}};
  }
  const auto refuse = [&](NcStatus status) {
    return std::unexpected{Error{fail(status, NcOp::get_var, name.view())}};
  };
  if (info->type != Type::string) {
    return refuse(WrapperFault::type_mismatch);
  }
  const Slab slab = whole(*info);
  const auto total = detail::check_slab(*info, slab, ctx.limits);
  if (not total) {
    return refuse(total.error());
  }
  std::vector<std::string> values;
  std::uintmax_t bytes = 0;
  auto done = detail::for_each_block(
      slab, ctx.limits.slab_elements, ctx.stop,
      [&](const detail::Block& block) -> std::expected<void, Error> {
        VarStrings strings{block.size};
        if (auto got =
                detail::nc_call(NcOp::get_var, name.view(), path_,
                                [&] {
                                  return nc_get_vara_string(
                                      *ncid_, info->id, block.start.data(),
                                      block.count.data(), strings.data());
                                });
            not got) {
          return std::unexpected{Error{got.error()}};
        }
        for (const char* raw : strings.strings()) {
          const std::string_view text =
              raw == nullptr ? std::string_view{} : std::string_view{raw};
          bytes += text.size();
          if (bytes > ctx.limits.max_text_bytes) {
            return std::unexpected{Error{
                fail(WrapperFault::too_large, NcOp::get_var, name.view())}};
          }
          values.emplace_back(text);
        }
        return {};
      });
  if (not done) {
    return std::unexpected{done.error()};
  }
  return values;
}

template std::expected<std::vector<double>, Error> File::read<double>(
    NcNameRef, const Slab&, const ReadContext&) const;
template std::expected<std::vector<float>, Error> File::read<float>(
    NcNameRef, const Slab&, const ReadContext&) const;
template std::expected<std::vector<std::int8_t>, Error> File::read<std::int8_t>(
    NcNameRef, const Slab&, const ReadContext&) const;
template std::expected<std::vector<std::int16_t>, Error>
File::read<std::int16_t>(NcNameRef, const Slab&, const ReadContext&) const;
template std::expected<std::vector<std::int32_t>, Error>
File::read<std::int32_t>(NcNameRef, const Slab&, const ReadContext&) const;
template std::expected<std::vector<std::int64_t>, Error>
File::read<std::int64_t>(NcNameRef, const Slab&, const ReadContext&) const;

}  // namespace mov::io::nc
