// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// File: defining and writing a new file, and write_netcdf_atomic (C16).

#include <netcdf.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "internal.hpp"
#include "mov/io/detail/atomic_file.hpp"
#include "mov/io/detail/checked_product.hpp"
#include "mov/io/netcdf/file.hpp"
#include "nc_call.hpp"

namespace mov::io::nc {

namespace {

template <class I>
int put_vara_int64(int ncid, int varid, std::span<const std::size_t> start,
                   std::span<const std::size_t> count, std::span<const I> v) {
  if constexpr (std::same_as<I, long long>) {
    return nc_put_vara_longlong(ncid, varid, start.data(), count.data(),
                                v.data());
  } else {
    const std::vector<long long> buffer(v.begin(), v.end());
    return nc_put_vara_longlong(ncid, varid, start.data(), count.data(),
                                buffer.data());
  }
}

template <Numeric T>
int put_vara(int ncid, int varid, std::span<const std::size_t> start,
             std::span<const std::size_t> count, std::span<const T> v) {
  if constexpr (std::same_as<T, double>) {
    return nc_put_vara_double(ncid, varid, start.data(), count.data(),
                              v.data());
  } else if constexpr (std::same_as<T, float>) {
    return nc_put_vara_float(ncid, varid, start.data(), count.data(), v.data());
  } else if constexpr (std::same_as<T, std::int8_t>) {
    return nc_put_vara_schar(ncid, varid, start.data(), count.data(), v.data());
  } else if constexpr (std::same_as<T, std::int16_t>) {
    return nc_put_vara_short(ncid, varid, start.data(), count.data(), v.data());
  } else if constexpr (std::same_as<T, std::int32_t>) {
    return nc_put_vara_int(ncid, varid, start.data(), count.data(), v.data());
  } else {
    return put_vara_int64(ncid, varid, start, count, v);
  }
}

// The _FillValue of a new variable, in its own type (nc_def_var_fill takes a
// pointer to one value of the variable's type). A long long copy for int64_t.
template <Numeric T>
int def_var_fill(int ncid, int varid, T fill) {
  if constexpr (std::same_as<T, std::int64_t> and
                not std::same_as<T, long long>) {
    const auto wide = static_cast<long long>(fill);
    return nc_def_var_fill(ncid, varid, NC_FILL, &wide);
  } else {
    return nc_def_var_fill(ncid, varid, NC_FILL, &fill);
  }
}

std::vector<int> ids_of(std::span<const DimInfo> dims) {
  std::vector<int> ids;
  ids.reserve(dims.size());
  std::ranges::transform(dims, std::back_inserter(ids),
                         [](const DimInfo& dim) { return dim.id; });
  return ids;
}

}  // namespace

std::expected<File, NcError> File::create(const std::filesystem::path& temp,
                                          const std::filesystem::path& target) {
  const auto error = [&target](NcStatus status) {
    return std::unexpected{NcError{
        .status = status, .op = NcOp::create, .object = {}, .file = target}};
  };
  const auto native = detail::nc_path(temp);
  if (not native) {
    return error(native.error());
  }
  int ncid = 0;
  if (const int status = detail::nc_status([&] {
        return nc_create(native->c_str(), NC_NETCDF4 | NC_NOCLOBBER, &ncid);
      });
      status != NC_NOERR) {
    return error(LibraryStatus{status});
  }
  return File{ncid, true, target, ReadLimits{}};
}

std::expected<DimInfo, NcError> File::define_dim(NcNameRef name,
                                                 std::size_t length) {
  if (length == 0) {
    return std::unexpected{
        fail(WrapperFault::zero_length_dim, NcOp::def_dim, name.view())};
  }
  return id(NcOp::def_dim, name.view()).and_then([&](int ncid) {
    int dimid = 0;
    return detail::nc_call(
               NcOp::def_dim, name.view(), path_,
               [&] { return nc_def_dim(ncid, name.c_str(), length, &dimid); })
        .transform([&] {
          return DimInfo{.id = dimid, .name = NcName{name}, .length = length};
        });
  });
}

std::expected<VarInfo, NcError> File::define_char_var(
    NcNameRef name, std::span<const DimInfo> dims) {
  return id(NcOp::def_var, name.view()).and_then([&](int ncid) {
    const std::vector<int> dimids = ids_of(dims);
    int varid = 0;
    return detail::nc_call(NcOp::def_var, name.view(), path_,
                           [&] {
                             return nc_def_var(ncid, name.c_str(), NC_CHAR,
                                               static_cast<int>(dimids.size()),
                                               dimids.data(), &varid);
                           })
        .transform([&] {
          return VarInfo{.id = varid,
                         .name = NcName{name},
                         .type = Type::char_,
                         .dims = {dims.begin(), dims.end()}};
        });
  });
}

std::expected<VarInfo, NcError> File::define_numeric_var(
    NcNameRef name, std::span<const DimInfo> dims, Type type,
    const std::optional<int>& deflate_level,
    const std::optional<std::vector<std::size_t>>& chunks,
    const std::function<int(int, int)>& set_fill) {
  const auto ncid = id(NcOp::def_var, name.view());
  if (not ncid) {
    return std::unexpected{ncid.error()};
  }
  if (chunks and chunks->size() != dims.size()) {
    return std::unexpected{
        fail(WrapperFault::rank_mismatch, NcOp::def_var, name.view())};
  }
  const std::vector<int> dimids = ids_of(dims);
  int varid = 0;
  const auto call = [&](const auto& fn) {
    return detail::nc_call(NcOp::def_var, name.view(), path_, fn);
  };
  // Each optional step succeeds when it has nothing to do.
  const auto when = [](bool wanted, const auto& step) {
    return [wanted, &step]() -> std::expected<void, NcError> {
      return wanted ? step() : std::expected<void, NcError>{};
    };
  };
  return call([&] {
           return nc_def_var(*ncid, name.c_str(), detail::to_nc_type(type),
                             static_cast<int>(dimids.size()), dimids.data(),
                             &varid);
         })
      .and_then(when(chunks.has_value(),
                     [&] {
                       return call([&] {
                         return nc_def_var_chunking(*ncid, varid, NC_CHUNKED,
                                                    chunks->data());
                       });
                     }))
      .and_then(when(deflate_level.has_value(),
                     [&] {
                       return call([&] {
                         return nc_def_var_deflate(*ncid, varid, 1, 1,
                                                   *deflate_level);
                       });
                     }))
      .and_then(
          when(static_cast<bool>(set_fill),
               [&] { return call([&] { return set_fill(*ncid, varid); }); }))
      .transform([&] {
        return VarInfo{.id = varid,
                       .name = NcName{name},
                       .type = type,
                       .dims = {dims.begin(), dims.end()}};
      });
}

template <Numeric T>
std::expected<VarInfo, NcError> File::define_var(NcNameRef name,
                                                 std::span<const DimInfo> dims,
                                                 const VarOptions<T>& opt) {
  std::function<int(int, int)> set_fill;
  if (opt.fill) {
    set_fill = [fill = *opt.fill](int ncid, int varid) {
      return def_var_fill<T>(ncid, varid, fill);
    };
  }
  return define_numeric_var(name, dims, type_of<T>, opt.deflate_level,
                            opt.chunks, set_fill);
}

std::expected<File::PutPlan, NcError> File::plan_put(NcNameRef name, Type type,
                                                     std::size_t size,
                                                     const Slab& slab) const {
  const auto info = var(name, NcOp::put_var);
  if (not info) {
    return std::unexpected{info.error()};
  }
  const auto refuse = [&](WrapperFault fault) {
    return std::unexpected{fail(fault, NcOp::put_var, name.view())};
  };
  if (info->type != type) {
    return refuse(WrapperFault::type_mismatch);
  }
  if (slab.size() != info->dims.size()) {
    return refuse(WrapperFault::rank_mismatch);
  }
  PutPlan plan{.varid = info->id, .start = {}, .count = {}};
  for (const DimRange& range : slab) {
    plan.start.push_back(range.start);
    plan.count.push_back(range.count);
  }
  const std::optional<std::size_t> total =
      io::detail::checked_product(plan.count);
  if (not total) {
    return refuse(WrapperFault::overflow);
  }
  if (*total != size) {
    return refuse(WrapperFault::count_mismatch);
  }
  return plan;
}

template <Numeric T>
std::expected<void, NcError> File::put(NcNameRef name, std::span<const T> data,
                                       const Slab& slab) {
  return plan_put(name, type_of<T>, data.size(), slab)
      .and_then([&](const PutPlan& plan) {
        return detail::nc_call(NcOp::put_var, name.view(), path_, [&] {
          return put_vara<T>(*ncid_, plan.varid, plan.start, plan.count, data);
        });
      });
}

std::expected<void, NcError> File::put_char_rows(
    NcNameRef name, std::span<const std::string> rows) {
  const auto info = var(name, NcOp::put_var);
  if (not info) {
    return std::unexpected{info.error()};
  }
  const auto refuse = [&](WrapperFault fault) {
    return std::unexpected{fail(fault, NcOp::put_var, name.view())};
  };
  if (info->type != Type::char_) {
    return refuse(WrapperFault::type_mismatch);
  }
  if (info->dims.size() != 2) {
    return refuse(WrapperFault::rank_mismatch);
  }
  if (rows.size() != info->dims[0].length) {
    return refuse(WrapperFault::count_mismatch);
  }
  const std::size_t stride = info->dims[1].length;
  if (std::ranges::any_of(rows, [stride](const std::string& row) {
        return row.size() > stride;
      })) {
    return refuse(WrapperFault::name_too_long);
  }
  const std::array<std::size_t, 2> count{rows.size(), stride};
  const std::optional<std::size_t> total = io::detail::checked_product(count);
  if (not total) {
    return refuse(WrapperFault::overflow);
  }
  // Each row is copied with its byte length and NUL-padded to the stride
  // (B15: v4 wrote a fixed count over shorter strings).
  std::string bytes(*total, '\0');
  for (std::size_t row = 0; row < rows.size(); ++row) {
    std::ranges::copy(
        rows[row], bytes.begin() + static_cast<std::ptrdiff_t>(row * stride));
  }
  if (bytes.empty()) {
    return {};
  }
  const std::array<std::size_t, 2> start{0, 0};
  return detail::nc_call(NcOp::put_var, name.view(), path_, [&] {
    return nc_put_vara_text(*ncid_, info->id, start.data(), count.data(),
                            bytes.data());
  });
}

std::expected<void, NcError> File::end_define() {
  return id(NcOp::enddef, {}).and_then([&](int ncid) {
    return detail::nc_call(NcOp::enddef, {}, path_,
                           [ncid] { return nc_enddef(ncid); });
  });
}

namespace detail {

std::expected<void, Error> write_netcdf_atomic_impl(
    const std::filesystem::path& target, const AtomicNcBody& body,
    const io::detail::FaultInjector& fault) {
  using io::detail::AtomicStage;
  const auto file_error = [&target](FileOp op, std::error_code ec) {
    return std::unexpected{
        Error{FileError{.op = op, .path = target, .ec = ec}}};
  };
  if (auto replaceable = io::detail::check_target_replaceable(target);
      not replaceable) {
    return std::unexpected{Error{replaceable.error()}};
  }
  const std::filesystem::path temp =
      io::detail::temp_path_for(target, fault.temp_suffix);
  if (fault.fails(AtomicStage::create)) {
    return file_error(FileOp::create, fault.code);
  }
  auto created = File::create(temp, target);
  if (not created) {
    // NC_NOCLOBBER: an existing file at `temp` is someone else's; keep it.
    return std::unexpected{Error{created.error()}};
  }
  // Declared before `file`, so the file is aborted or closed before it is
  // removed.
  io::detail::TempFileGuard guard{temp};
  File file = *std::move(created);
  if (auto written = body(file); not written) {
    file.abort();
    return written;
  }
  if (fault.fails(AtomicStage::body)) {
    file.abort();
    return file_error(FileOp::write, fault.code);
  }
  if (auto closed = std::move(file).close(); not closed) {
    return std::unexpected{Error{closed.error()}};
  }
  if (fault.fails(AtomicStage::close)) {
    return file_error(FileOp::close, fault.code);
  }
  if (auto committed = io::detail::commit_temp(target, temp, fault);
      not committed) {
    return std::unexpected{Error{committed.error()}};
  }
  guard.release();
  return {};
}

}  // namespace detail

template std::expected<VarInfo, NcError> File::define_var<double>(
    NcNameRef, std::span<const DimInfo>, const VarOptions<double>&);
template std::expected<VarInfo, NcError> File::define_var<float>(
    NcNameRef, std::span<const DimInfo>, const VarOptions<float>&);
template std::expected<VarInfo, NcError> File::define_var<std::int8_t>(
    NcNameRef, std::span<const DimInfo>, const VarOptions<std::int8_t>&);
template std::expected<VarInfo, NcError> File::define_var<std::int16_t>(
    NcNameRef, std::span<const DimInfo>, const VarOptions<std::int16_t>&);
template std::expected<VarInfo, NcError> File::define_var<std::int32_t>(
    NcNameRef, std::span<const DimInfo>, const VarOptions<std::int32_t>&);
template std::expected<VarInfo, NcError> File::define_var<std::int64_t>(
    NcNameRef, std::span<const DimInfo>, const VarOptions<std::int64_t>&);

template std::expected<void, NcError> File::put<double>(NcNameRef,
                                                        std::span<const double>,
                                                        const Slab&);
template std::expected<void, NcError> File::put<float>(NcNameRef,
                                                       std::span<const float>,
                                                       const Slab&);
template std::expected<void, NcError> File::put<std::int8_t>(
    NcNameRef, std::span<const std::int8_t>, const Slab&);
template std::expected<void, NcError> File::put<std::int16_t>(
    NcNameRef, std::span<const std::int16_t>, const Slab&);
template std::expected<void, NcError> File::put<std::int32_t>(
    NcNameRef, std::span<const std::int32_t>, const Slab&);
template std::expected<void, NcError> File::put<std::int64_t>(
    NcNameRef, std::span<const std::int64_t>, const Slab&);

}  // namespace mov::io::nc
