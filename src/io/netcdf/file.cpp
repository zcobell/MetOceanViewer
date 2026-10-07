// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// File: opening, closing, the move operations and the structure queries.

#include "mov/io/netcdf/file.hpp"

#include <netcdf.h>
#include <netcdf_meta.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "internal.hpp"
#include "nc_call.hpp"

static_assert(mov::io::nc::nc_max_name == NC_MAX_NAME);
static_assert(mov::io::nc::detail::nc_noerr == NC_NOERR);
// NOLINTNEXTLINE(misc-redundant-expression): a macro that must be 1
static_assert(NC_HAS_HDF5 == 1, "netCDF-C must be built with netCDF-4/HDF5");

namespace mov::io::nc {

namespace detail {

Type to_type(int xtype) noexcept {
  switch (xtype) {
    case NC_BYTE:
      return Type::byte;
    case NC_UBYTE:
      return Type::ubyte;
    case NC_CHAR:
      return Type::char_;
    case NC_SHORT:
      return Type::short_;
    case NC_USHORT:
      return Type::ushort;
    case NC_INT:
      return Type::int_;
    case NC_UINT:
      return Type::uint;
    case NC_INT64:
      return Type::int64;
    case NC_UINT64:
      return Type::uint64;
    case NC_FLOAT:
      return Type::float_;
    case NC_DOUBLE:
      return Type::double_;
    case NC_STRING:
      return Type::string;
    default:
      return Type::other;
  }
}

int to_nc_type(Type type) noexcept {
  constexpr std::array<int, 13> ids{
      NC_BYTE,  NC_UBYTE,  NC_CHAR,  NC_SHORT,  NC_USHORT, NC_INT, NC_UINT,
      NC_INT64, NC_UINT64, NC_FLOAT, NC_DOUBLE, NC_STRING, NC_NAT};
  return ids.at(static_cast<std::size_t>(type));
}

std::expected<void, NcError> status_to_expected(
    int status, NcOp op, std::string_view object,
    const std::filesystem::path& file) {
  if (status == NC_NOERR) {
    return {};
  }
  return std::unexpected{NcError{.status = LibraryStatus{status},
                                 .op = op,
                                 .object = std::string{object},
                                 .file = file}};
}

std::string att_object(const AttTarget& on, NcNameRef att) {
  const std::optional<NcNameRef> variable = on.variable();
  std::string object{variable ? variable->view() : std::string_view{}};
  object += ':';
  object += att.view();
  return object;
}

}  // namespace detail

namespace {

// A name the library returned; it cannot be invalid unless the file is.
std::expected<NcName, int> name_from(const char* text) {
  return NcName::make(text).transform_error(
      [](NcNameError /*unused*/) { return NC_EBADNAME; });
}

// netCDF-C reports system errors as positive errno values; an open of
// anything but a regular file is refused before netCDF-C sees it (a FIFO
// would block in open()).
std::optional<int> not_a_regular_file(const std::filesystem::path& path) {
  std::error_code ec;
  const std::filesystem::file_status status = std::filesystem::status(path, ec);
  if (ec or not std::filesystem::exists(status) or
      std::filesystem::is_regular_file(status)) {
    return std::nullopt;  // netCDF-C reports a missing file itself
  }
  return std::filesystem::is_directory(status) ? EISDIR : EINVAL;
}

}  // namespace

File::File(int ncid, bool writable, std::filesystem::path path,
           const ReadLimits& limits) noexcept
    : ncid_{ncid},
      writable_{writable},
      path_{std::move(path)},
      limits_{limits} {}

std::expected<File, NcError> File::open(const std::filesystem::path& path,
                                        const ReadLimits& limits) {
  const auto error = [&path](NcStatus status) {
    return std::unexpected{NcError{
        .status = status, .op = NcOp::open, .object = {}, .file = path}};
  };
  if (const std::optional<int> refused = not_a_regular_file(path)) {
    return error(LibraryStatus{*refused});
  }
  const auto native = detail::nc_path(path);
  if (not native) {
    return error(native.error());
  }
  int ncid = 0;
  if (const int status = detail::nc_status(
          [&] { return nc_open(native->c_str(), NC_NOWRITE, &ncid); });
      status != NC_NOERR) {
    return error(LibraryStatus{status});
  }
  return File{ncid, false, path, limits};
}

File::File(File&& other) noexcept
    : ncid_{std::exchange(other.ncid_, std::nullopt)},
      writable_{other.writable_},
      path_{std::move(other.path_)},
      limits_{other.limits_} {}

File& File::operator=(File&& other) noexcept {
  if (this != &other) {
    release();
    ncid_ = std::exchange(other.ncid_, std::nullopt);
    writable_ = other.writable_;
    path_ = std::move(other.path_);
    limits_ = other.limits_;
  }
  return *this;
}

File::~File() { release(); }

void File::abort() noexcept {
  if (const std::optional<int> ncid = std::exchange(ncid_, std::nullopt)) {
    // nc_abort always releases the id; its status has nothing to add.
    static_cast<void>(
        detail::nc_status([ncid = *ncid] { return nc_abort(ncid); }));
  }
}

void File::release() noexcept {
  if (not ncid_) {
    return;
  }
  if (writable_) {
    abort();  // a write handle destroyed open: the content is incomplete
    return;
  }
  const int ncid = *ncid_;
  if (detail::nc_status([ncid] { return nc_close(ncid); }) == NC_NOERR) {
    ncid_.reset();
    return;
  }
  abort();  // a failed close keeps the id; abort releases it
}

std::expected<void, NcError> File::close() && {
  const std::expected<int, NcError> ncid = id(NcOp::close, {});
  if (not ncid) {
    return std::unexpected{ncid.error()};
  }
  const int status =
      detail::nc_status([ncid = *ncid] { return nc_close(ncid); });
  if (status == NC_NOERR) {
    ncid_.reset();
    return {};
  }
  abort();
  return std::unexpected{fail(LibraryStatus{status}, NcOp::close, {})};
}

NcError File::fail(NcStatus status, NcOp op, std::string_view object) const {
  return NcError{
      .status = status, .op = op, .object = std::string{object}, .file = path_};
}

std::expected<int, NcError> File::id(NcOp op, std::string_view object) const {
  if (not ncid_) {
    return std::unexpected{fail(WrapperFault::closed, op, object)};
  }
  return *ncid_;
}

std::expected<DimInfo, NcError> File::dim_info(int dimid, NcOp op) const {
  return id(op, {}).and_then([&](int ncid) -> std::expected<DimInfo, NcError> {
    std::array<char, NC_MAX_NAME + 1> name{};
    std::size_t length = 0;
    if (auto done = detail::nc_call(
            op, {}, path_,
            [&] { return nc_inq_dim(ncid, dimid, name.data(), &length); });
        not done) {
      return std::unexpected{done.error()};
    }
    return name_from(name.data())
        .transform([&](NcName&& valid) {
          return DimInfo{
              .id = dimid, .name = std::move(valid), .length = length};
        })
        .transform_error([&](int status) {
          return fail(LibraryStatus{status}, op, name.data());
        });
  });
}

std::expected<VarInfo, NcError> File::var_info(int varid, NcOp op) const {
  const std::expected<int, NcError> ncid = id(op, {});
  if (not ncid) {
    return std::unexpected{ncid.error()};
  }
  std::array<char, NC_MAX_NAME + 1> name{};
  nc_type xtype = NC_NAT;
  int ndims = 0;
  if (auto done = detail::nc_call(op, {}, path_,
                                  [&] {
                                    return nc_inq_var(*ncid, varid, name.data(),
                                                      &xtype, &ndims, nullptr,
                                                      nullptr);
                                  });
      not done) {
    return std::unexpected{done.error()};
  }
  std::vector<int> dimids(static_cast<std::size_t>(ndims));
  if (auto done = detail::nc_call(
          op, name.data(), path_,
          [&] { return nc_inq_vardimid(*ncid, varid, dimids.data()); });
      not done) {
    return std::unexpected{done.error()};
  }
  std::vector<DimInfo> dims;
  dims.reserve(dimids.size());
  for (const int dimid : dimids) {
    auto dim = dim_info(dimid, op);
    if (not dim) {
      return std::unexpected{dim.error()};
    }
    dims.push_back(*std::move(dim));
  }
  auto valid = name_from(name.data());
  if (not valid) {
    return std::unexpected{fail(LibraryStatus{valid.error()}, op, name.data())};
  }
  return VarInfo{.id = varid,
                 .name = *std::move(valid),
                 .type = detail::to_type(xtype),
                 .dims = std::move(dims)};
}

std::expected<VarInfo, NcError> File::var(NcNameRef name, NcOp op) const {
  return id(op, name.view()).and_then([&](int ncid) {
    int varid = 0;
    return detail::nc_call(
               op, name.view(), path_,
               [&] { return nc_inq_varid(ncid, name.c_str(), &varid); })
        .and_then([&] { return var_info(varid, op); });
  });
}

std::expected<std::optional<DimInfo>, NcError> File::find_dim(
    NcNameRef name) const {
  using Result = std::expected<std::optional<DimInfo>, NcError>;
  return id(NcOp::inquire, name.view()).and_then([&](int ncid) -> Result {
    int dimid = 0;
    const int status = detail::nc_status(
        [&] { return nc_inq_dimid(ncid, name.c_str(), &dimid); });
    if (status == NC_EBADDIM) {
      return std::nullopt;
    }
    if (status != NC_NOERR) {
      return std::unexpected{
          fail(LibraryStatus{status}, NcOp::inquire, name.view())};
    }
    return dim_info(dimid, NcOp::inquire);
  });
}

std::expected<std::optional<VarInfo>, NcError> File::find_var(
    NcNameRef name) const {
  using Result = std::expected<std::optional<VarInfo>, NcError>;
  return id(NcOp::inquire, name.view()).and_then([&](int ncid) -> Result {
    int varid = 0;
    const int status = detail::nc_status(
        [&] { return nc_inq_varid(ncid, name.c_str(), &varid); });
    if (status == NC_ENOTVAR) {
      return std::nullopt;
    }
    if (status != NC_NOERR) {
      return std::unexpected{
          fail(LibraryStatus{status}, NcOp::inquire, name.view())};
    }
    return var_info(varid, NcOp::inquire);
  });
}

std::expected<std::vector<VarInfo>, NcError> File::variables() const {
  using Result = std::expected<std::vector<VarInfo>, NcError>;
  return id(NcOp::inquire, {}).and_then([&](int ncid) -> Result {
    int count = 0;
    if (auto done = detail::nc_call(
            NcOp::inquire, {}, path_,
            [&] { return nc_inq_varids(ncid, &count, nullptr); });
        not done) {
      return std::unexpected{done.error()};
    }
    std::vector<int> varids(static_cast<std::size_t>(count));
    if (auto done = detail::nc_call(
            NcOp::inquire, {}, path_,
            [&] { return nc_inq_varids(ncid, &count, varids.data()); });
        not done) {
      return std::unexpected{done.error()};
    }
    std::vector<VarInfo> vars;
    vars.reserve(varids.size());
    for (const int varid : varids) {
      auto info = var_info(varid, NcOp::inquire);
      if (not info) {
        return std::unexpected{info.error()};
      }
      vars.push_back(*std::move(info));
    }
    return vars;
  });
}

}  // namespace mov::io::nc
