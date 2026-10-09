// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// NewFile: defining and writing a new file, and write_netcdf_atomic.

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
#include <string_view>
#include <system_error>
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

std::expected<int, NcError> NewFile::create(
    const std::filesystem::path& temp, const std::filesystem::path& target) {
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
  return ncid;
}

NewFile::~NewFile() { abandon(); }

void NewFile::abandon() noexcept {
  if (not ncid_) {
    return;
  }
  const int ncid = *ncid_;
  forget();
  // Out of define mode first (netCDF-C clears the flag before anything that
  // can fail), so nc_abort deletes nothing: in define mode NC4_abort would
  // remove() its own copy of the path, cut at NC_MAX_NAME bytes.
  static_cast<void>(detail::nc_status([ncid] { return nc_enddef(ncid); }));
  static_cast<void>(detail::nc_status([ncid] { return nc_abort(ncid); }));
}

std::expected<void, NcError> NewFile::finish() {
  const auto ncid = id(NcOp::close, {});
  if (not ncid) {
    return std::unexpected{ncid.error()};
  }
  // A failed sync has released nothing: the file can still be aborted.
  if (auto synced = detail::nc_call(NcOp::sync, {}, path_,
                                    [&] { return nc_sync(*ncid); });
      not synced) {
    abandon();
    return synced;
  }
  // A failed close is never followed by nc_abort (see File): the id is
  // given up and its entry stays in netCDF-C.
  forget();
  return detail::nc_call(NcOp::close, {}, path_,
                         [&] { return nc_close(*ncid); });
}

std::expected<DimInfo, NcError> NewFile::define_dim(NcNameRef name,
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
          return DimInfo{.id = dimid,
                         .name = NcName{name},
                         .length = length,
                         .unlimited = false};
        });
  });
}

std::expected<VarInfo, NcError> NewFile::define_char_var(
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

std::expected<VarInfo, NcError> NewFile::define_numeric_var(
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
  const auto call = [&](const auto& fn) {
    return detail::nc_call(NcOp::def_var, name.view(), path_, fn);
  };
  const std::vector<int> dimids = ids_of(dims);
  int varid = 0;
  if (auto defined = call([&] {
        return nc_def_var(*ncid, name.c_str(), detail::to_nc_type(type),
                          static_cast<int>(dimids.size()), dimids.data(),
                          &varid);
      });
      not defined) {
    return std::unexpected{defined.error()};
  }
  if (chunks) {
    if (auto done = call([&] {
          return nc_def_var_chunking(*ncid, varid, NC_CHUNKED, chunks->data());
        });
        not done) {
      return std::unexpected{done.error()};
    }
  }
  if (deflate_level) {
    if (auto done = call([&] {
          return nc_def_var_deflate(*ncid, varid, 1, 1, *deflate_level);
        });
        not done) {
      return std::unexpected{done.error()};
    }
  }
  if (set_fill) {
    if (auto done = call([&] { return set_fill(*ncid, varid); }); not done) {
      return std::unexpected{done.error()};
    }
  }
  return VarInfo{.id = varid,
                 .name = NcName{name},
                 .type = type,
                 .dims = {dims.begin(), dims.end()}};
}

template <Numeric T>
std::expected<VarInfo, NcError> NewFile::define_var(
    NcNameRef name, std::span<const DimInfo> dims, const VarOptions<T>& opt) {
  std::function<int(int, int)> set_fill;
  if (opt.fill) {
    set_fill = [fill = *opt.fill](int ncid, int varid) {
      return def_var_fill<T>(ncid, varid, fill);
    };
  }
  return define_numeric_var(name, dims, type_of<T>, opt.deflate_level,
                            opt.chunks, set_fill);
}

std::expected<NewFile::PutPlan, NcError> NewFile::plan_put(
    NcNameRef name, Type type, std::size_t size, const Slab& slab) const {
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
  PutPlan plan{.varid = info->id, .slab = detail::unzip(slab)};
  const std::optional<std::size_t> total =
      io::detail::checked_product(plan.slab.count);
  if (not total) {
    return refuse(WrapperFault::overflow);
  }
  if (*total != size) {
    return refuse(WrapperFault::count_mismatch);
  }
  return plan;
}

template <Numeric T>
std::expected<void, NcError> NewFile::put(NcNameRef name,
                                          std::span<const T> data,
                                          const Slab& slab) {
  return plan_put(name, type_of<T>, data.size(), slab)
      .and_then([&](const PutPlan& plan) {
        return detail::nc_call(NcOp::put_var, name.view(), path_, [&] {
          return put_vara<T>(*ncid_, plan.varid, plan.slab.start,
                             plan.slab.count, data);
        });
      });
}

std::expected<void, NcError> NewFile::put_char_rows_impl(
    NcNameRef name, std::span<const std::string_view> rows) {
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
  if (std::ranges::any_of(rows, [stride](std::string_view row) {
        return row.size() > stride;
      })) {
    return refuse(WrapperFault::name_too_long);
  }
  const std::array<std::size_t, 2> count{rows.size(), stride};
  const std::optional<std::size_t> total = io::detail::checked_product(count);
  if (not total) {
    return refuse(WrapperFault::overflow);
  }
  if (*total == 0) {
    return {};
  }
  // Each row is copied with its byte length and NUL-padded to the stride
  // (v4 wrote a fixed count over shorter strings).
  std::string bytes(*total, '\0');
  for (std::size_t row = 0; row < rows.size(); ++row) {
    std::ranges::copy(
        rows[row], bytes.begin() + static_cast<std::ptrdiff_t>(row * stride));
  }
  const std::array<std::size_t, 2> start{0, 0};
  return detail::nc_call(NcOp::put_var, name.view(), path_, [&] {
    return nc_put_vara_text(*ncid_, info->id, start.data(), count.data(),
                            bytes.data());
  });
}

std::expected<void, NcError> NewFile::end_define() {
  return id(NcOp::enddef, {}).and_then([&](int ncid) {
    return detail::nc_call(NcOp::enddef, {}, path_,
                           [ncid] { return nc_enddef(ncid); });
  });
}

namespace detail {

std::expected<void, Error> write_netcdf_atomic_impl(
    const std::filesystem::path& target, const ReadLimits& limits,
    const NcBodyFunction& body, const io::detail::FaultInjector& fault) {
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
  const auto ncid = NewFile::create(temp, target);
  if (not ncid) {
    // NC_NOCLOBBER: an existing file at `temp` is someone else's; keep it.
    return std::unexpected{Error{ncid.error()}};
  }
  // Declared before `file`, so the file is abandoned or closed before it is
  // removed.
  io::detail::TempFileGuard guard{temp};
  NewFile file{*ncid, target, limits};
  if (auto written = body(file); not written) {
    file.abandon();
    return written;
  }
  if (fault.fails(AtomicStage::body)) {
    file.abandon();
    return file_error(FileOp::write, fault.code);
  }
  if (auto finished = file.finish(); not finished) {
    return std::unexpected{Error{finished.error()}};
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

template std::expected<VarInfo, NcError> NewFile::define_var<double>(
    NcNameRef, std::span<const DimInfo>, const VarOptions<double>&);
template std::expected<void, NcError> NewFile::put<double>(
    NcNameRef, std::span<const double>, const Slab&);
template std::expected<VarInfo, NcError> NewFile::define_var<float>(
    NcNameRef, std::span<const DimInfo>, const VarOptions<float>&);
template std::expected<void, NcError> NewFile::put<float>(
    NcNameRef, std::span<const float>, const Slab&);
template std::expected<VarInfo, NcError> NewFile::define_var<std::int8_t>(
    NcNameRef, std::span<const DimInfo>, const VarOptions<std::int8_t>&);
template std::expected<void, NcError> NewFile::put<std::int8_t>(
    NcNameRef, std::span<const std::int8_t>, const Slab&);
template std::expected<VarInfo, NcError> NewFile::define_var<std::int16_t>(
    NcNameRef, std::span<const DimInfo>, const VarOptions<std::int16_t>&);
template std::expected<void, NcError> NewFile::put<std::int16_t>(
    NcNameRef, std::span<const std::int16_t>, const Slab&);
template std::expected<VarInfo, NcError> NewFile::define_var<std::int32_t>(
    NcNameRef, std::span<const DimInfo>, const VarOptions<std::int32_t>&);
template std::expected<void, NcError> NewFile::put<std::int32_t>(
    NcNameRef, std::span<const std::int32_t>, const Slab&);
template std::expected<VarInfo, NcError> NewFile::define_var<std::int64_t>(
    NcNameRef, std::span<const DimInfo>, const VarOptions<std::int64_t>&);
template std::expected<void, NcError> NewFile::put<std::int64_t>(
    NcNameRef, std::span<const std::int64_t>, const Slab&);

}  // namespace mov::io::nc
