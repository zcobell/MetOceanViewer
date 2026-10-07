// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// File: bulk data reads.

#include <netcdf.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
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
                                                const ReadLimits& limits,
                                                std::size_t element_bytes) {
  if (slab.size() != var.dims.size()) {
    return std::unexpected{WrapperFault::rank_mismatch};
  }
  for (std::size_t i = 0; i < slab.size(); ++i) {
    const std::size_t length = var.dims[i].length;
    if (slab[i].start > length) {
      return std::unexpected{LibraryStatus{NC_EINVALCOORDS}};
    }
    if (slab[i].count > length - slab[i].start) {
      return std::unexpected{LibraryStatus{NC_EEDGE}};
    }
  }
  const std::optional<std::size_t> total =
      io::detail::checked_product(unzip(slab).count);
  if (not total) {
    return std::unexpected{WrapperFault::overflow};
  }
  if (*total > limits.max_elements or
      not fits_bytes(*total, element_bytes, 0, limits.max_result_bytes)) {
    return std::unexpected{WrapperFault::too_large};
  }
  return *total;
}

}  // namespace detail

namespace {

// int64_t is long on LP64 Linux and long long elsewhere; netCDF-C takes long
// long, so a long buffer is read through a copy.
template <class I>
int get_vara_int64(int ncid, int varid, const std::size_t* start,
                   const std::size_t* count, std::span<I> out) {
  if constexpr (std::same_as<I, long long>) {
    return nc_get_vara_longlong(ncid, varid, start, count, out.data());
  } else {
    std::vector<long long> buffer(out.size());
    const int status =
        nc_get_vara_longlong(ncid, varid, start, count, buffer.data());
    std::ranges::copy(buffer, out.begin());
    return status;
  }
}

// nc_get_vara_<T> into `out` (one block), converting from the variable's
// type (readable_as).
template <Numeric T>
int get_vara(int ncid, int varid, std::span<const std::size_t> start_span,
             std::span<const std::size_t> count_span, std::span<T> out) {
  const std::size_t* start = start_span.data();
  const std::size_t* count = count_span.data();
  if constexpr (std::same_as<T, double>) {
    return nc_get_vara_double(ncid, varid, start, count, out.data());
  } else if constexpr (std::same_as<T, float>) {
    return nc_get_vara_float(ncid, varid, start, count, out.data());
  } else if constexpr (std::same_as<T, std::int8_t>) {
    return nc_get_vara_schar(ncid, varid, start, count, out.data());
  } else if constexpr (std::same_as<T, std::int16_t>) {
    return nc_get_vara_short(ncid, varid, start, count, out.data());
  } else if constexpr (std::same_as<T, std::int32_t>) {
    return nc_get_vara_int(ncid, varid, start, count, out.data());
  } else {
    return get_vara_int64(ncid, varid, start, count, out);
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
    NcNameRef name, const Slab& slab, bool (*readable)(Type) noexcept,
    std::size_t element_bytes) const {
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
  const auto total = detail::check_slab(*info, slab, limits_, element_bytes);
  if (not total) {
    return refuse(total.error());
  }
  return ReadPlan{.varid = info->id, .total = *total};
}

template <Numeric T>
std::expected<std::vector<T>, Error> File::read(NcNameRef name,
                                                const Slab& slab,
                                                const StopToken& stop) const {
  using Result = std::expected<std::vector<T>, Error>;
  return plan_read(name, slab, &readable_as<T>, sizeof(T))
      .and_then([&](const ReadPlan& plan) -> Result {
        // Each block is read straight into its place in the result.
        std::vector<T> values(plan.total);
        const std::span<T> out{values};
        return for_each_block(
                   slab, stop,
                   [&](const Block& block) -> std::expected<void, Error> {
                     return detail::nc_call(
                                NcOp::get_var, name.view(), path_,
                                [&] {
                                  return get_vara<T>(
                                      *ncid_, plan.varid, block.start,
                                      block.count,
                                      out.subspan(block.offset, block.size));
                                })
                         .transform_error(lift<Error>);
                   })
            .transform([&] { return std::move(values); });
      });
}

template <Numeric T>
std::expected<void, Error> File::stream(NcNameRef name, int varid,
                                        const Slab& slab, const StopToken& stop,
                                        const BlockVisitor<T>& visit) const {
  std::vector<T> buffer;  // one block, reused
  return for_each_block(
      slab, stop, [&](const Block& block) -> std::expected<void, Error> {
        buffer.resize(block.size);
        return detail::nc_call(NcOp::get_var, name.view(), path_,
                               [&] {
                                 return get_vara<T>(*ncid_, varid, block.start,
                                                    block.count,
                                                    std::span{buffer});
                               })
            .transform_error(lift<Error>)
            .and_then(
                [&] { return visit(std::span<const T>{buffer}, block.outer); });
      });
}

template <Numeric T>
std::expected<void, Error> File::read_blocks(NcNameRef name, const Slab& slab,
                                             const BlockVisitor<T>& visit,
                                             const StopToken& stop) const {
  return plan_read(name, slab, &readable_as<T>, sizeof(T))
      .and_then([&](const ReadPlan& plan) {
        return stream<T>(name, plan.varid, slab, stop, visit);
      });
}

std::expected<std::vector<core::Sample>, Error> File::read_samples(
    NcNameRef name, const Slab& slab, const StopToken& stop) const {
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
        const auto mask = masking<T>(name);
        if (not mask) {
          return std::unexpected{Error{mask.error()}};
        }
        return plan_read(name, slab, &readable_as<T>, sizeof(core::Sample))
            .and_then([&](const ReadPlan& plan) {
              // Masked block by block into the result: the raw values never
              // exist all at once.
              std::vector<core::Sample> samples;
              samples.reserve(plan.total);
              return stream<T>(name, plan.varid, slab, stop,
                               [&](std::span<const T> raw, DimRange) {
                                 std::ranges::transform(
                                     raw, std::back_inserter(samples),
                                     [&](T v) { return mask->apply(v); });
                                 return std::expected<void, Error>{};
                               })
                  .transform([&] { return std::move(samples); });
            });
      },
      type_mismatch);
}

std::expected<std::vector<std::string>, Error> File::read_char_rows(
    NcNameRef name, const StopToken& stop) const {
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
  if (info->dims.empty()) {
    return refuse(WrapperFault::rank_mismatch);
  }
  const Slab slab = whole(*info);
  // The bytes are read once and copied into the rows: two bytes each.
  const auto total = detail::check_slab(*info, slab, limits_, 2);
  if (not total) {
    return refuse(total.error());
  }
  const std::size_t stride = info->dims.back().length;
  const std::size_t outer_rank = info->dims.size() - 1;
  std::vector<std::size_t> outer_counts(outer_rank);
  std::ranges::transform(std::span{slab}.first(outer_rank),
                         outer_counts.begin(), &DimRange::count);
  // With a zero-length stride the rows have no bytes to bound them.
  const std::optional<std::size_t> rows =
      io::detail::checked_product(outer_counts);
  if (not rows or *rows > limits_.max_elements or
      not detail::fits_bytes(*rows, sizeof(std::string), 2 * *total,
                             limits_.max_result_bytes)) {
    return refuse(WrapperFault::too_large);
  }
  std::string bytes(*total, '\0');
  const std::span<char> out{bytes};
  if (auto done = for_each_block(
          slab, stop,
          [&](const Block& block) -> std::expected<void, Error> {
            return detail::nc_call(
                       NcOp::get_var, name.view(), path_,
                       [&] {
                         return nc_get_vara_text(
                             *ncid_, info->id, block.start.data(),
                             block.count.data(),
                             out.subspan(block.offset, block.size).data());
                       })
                .transform_error(lift<Error>);
          });
      not done) {
    return std::unexpected{done.error()};
  }
  std::vector<std::string> result;
  result.reserve(*rows);
  const std::string_view all{bytes};
  for (std::size_t row = 0; row < *rows; ++row) {
    result.emplace_back(all.substr(row * stride, stride));
  }
  return result;
}

std::expected<std::vector<std::string>, Error> File::read_strings(
    NcNameRef name, const StopToken& stop) const {
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
  const auto total =
      detail::check_slab(*info, slab, limits_, sizeof(std::string));
  if (not total) {
    return refuse(total.error());
  }
  std::vector<std::string> values;
  values.reserve(*total);
  // The result's bytes: the string objects, then each string's text.
  std::size_t used = *total * sizeof(std::string);
  auto done = for_each_block(
      slab, stop, [&](const Block& block) -> std::expected<void, Error> {
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
          if (text.size() > limits_.max_result_bytes - used) {
            return std::unexpected{Error{
                fail(WrapperFault::too_large, NcOp::get_var, name.view())}};
          }
          used += text.size();
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
    NcNameRef, const Slab&, const StopToken&) const;
template std::expected<std::vector<float>, Error> File::read<float>(
    NcNameRef, const Slab&, const StopToken&) const;
template std::expected<std::vector<std::int8_t>, Error> File::read<std::int8_t>(
    NcNameRef, const Slab&, const StopToken&) const;
template std::expected<std::vector<std::int16_t>, Error>
File::read<std::int16_t>(NcNameRef, const Slab&, const StopToken&) const;
template std::expected<std::vector<std::int32_t>, Error>
File::read<std::int32_t>(NcNameRef, const Slab&, const StopToken&) const;
template std::expected<std::vector<std::int64_t>, Error>
File::read<std::int64_t>(NcNameRef, const Slab&, const StopToken&) const;

template std::expected<void, Error> File::read_blocks<double>(
    NcNameRef, const Slab&, const BlockVisitor<double>&,
    const StopToken&) const;
template std::expected<void, Error> File::read_blocks<float>(
    NcNameRef, const Slab&, const BlockVisitor<float>&, const StopToken&) const;
template std::expected<void, Error> File::read_blocks<std::int8_t>(
    NcNameRef, const Slab&, const BlockVisitor<std::int8_t>&,
    const StopToken&) const;
template std::expected<void, Error> File::read_blocks<std::int16_t>(
    NcNameRef, const Slab&, const BlockVisitor<std::int16_t>&,
    const StopToken&) const;
template std::expected<void, Error> File::read_blocks<std::int32_t>(
    NcNameRef, const Slab&, const BlockVisitor<std::int32_t>&,
    const StopToken&) const;
template std::expected<void, Error> File::read_blocks<std::int64_t>(
    NcNameRef, const Slab&, const BlockVisitor<std::int64_t>&,
    const StopToken&) const;

}  // namespace mov::io::nc
