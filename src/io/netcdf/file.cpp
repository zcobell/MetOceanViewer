// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Dataset (the shared inquiries) and File: opening, closing, moves and the
// structure queries.

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
#include "mov/io/detail/traverse.hpp"
#include "nc_call.hpp"

static_assert(mov::io::nc::nc_max_name == NC_MAX_NAME);
static_assert(mov::io::nc::detail::nc_noerr == NC_NOERR);
// NOLINTNEXTLINE(misc-redundant-expression): a macro that must be 1
static_assert(NC_HAS_HDF5 == 1, "netCDF-C must be built with netCDF-4/HDF5");

namespace mov::io::nc {

namespace detail {

namespace {

// Type, in its enumerator order, and the netCDF-C type id of each.
constexpr std::array<int, 13> nc_type_ids{
    NC_BYTE,  NC_UBYTE,  NC_CHAR,  NC_SHORT,  NC_USHORT, NC_INT, NC_UINT,
    NC_INT64, NC_UINT64, NC_FLOAT, NC_DOUBLE, NC_STRING, NC_NAT};

constexpr Type type_of_id(int xtype) noexcept {
  for (std::size_t i = 0; i + 1 < nc_type_ids.size(); ++i) {
    if (nc_type_ids.at(i) == xtype) {
      return static_cast<Type>(i);
    }
  }
  return Type::other;
}

// The two maps are inverse on every type but `other`, which has no id.
constexpr bool bijective() noexcept {
  for (std::size_t i = 0; i + 1 < nc_type_ids.size(); ++i) {
    if (type_of_id(nc_type_ids.at(i)) != static_cast<Type>(i)) {
      return false;
    }
  }
  return type_of_id(NC_NAT) == Type::other and
         static_cast<std::size_t>(Type::other) + 1 == nc_type_ids.size();
}
static_assert(bijective());

}  // namespace

Type to_type(int xtype) noexcept { return type_of_id(xtype); }

int to_nc_type(Type type) noexcept {
  return nc_type_ids.at(static_cast<std::size_t>(type));
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

// ---- Dataset ---------------------------------------------------------------

Dataset::Dataset(int ncid, std::filesystem::path path,
                 const ReadLimits& limits) noexcept
    : ncid_{ncid}, path_{std::move(path)}, limits_{limits} {}

// The path is copied, not moved: a moved-from handle still names its file in
// the `closed` errors it gives.
Dataset::Dataset(Dataset&& other) noexcept
    : ncid_{std::exchange(other.ncid_, std::nullopt)},
      // NOLINTNEXTLINE(performance-move-constructor-init): kept on purpose
      path_{other.path_},
      limits_{other.limits_} {}

Dataset& Dataset::operator=(Dataset&& other) noexcept {
  ncid_ = std::exchange(other.ncid_, std::nullopt);
  path_ = other.path_;
  limits_ = other.limits_;
  return *this;
}

NcError Dataset::fail(NcStatus status, NcOp op, std::string_view object) const {
  return NcError{
      .status = status, .op = op, .object = std::string{object}, .file = path_};
}

NcError Dataset::att_fail(NcStatus status, NcOp op, AttTarget on,
                          NcNameRef name) const {
  return fail(status, op, att_object(on, name));
}

std::expected<void, NcError> Dataset::att_status(int status, NcOp op,
                                                 AttTarget on,
                                                 NcNameRef name) const {
  if (status == NC_NOERR) {
    return {};
  }
  return std::unexpected{att_fail(LibraryStatus{status}, op, on, name)};
}

std::expected<int, NcError> Dataset::id(NcOp op,
                                        std::string_view object) const {
  if (not ncid_) {
    return std::unexpected{fail(WrapperFault::closed, op, object)};
  }
  return *ncid_;
}

namespace {

// A name the library returned; it cannot be invalid unless the file is.
std::expected<NcName, int> name_from(const char* text) {
  return NcName::make(text).transform_error(
      [](NcNameError /*unused*/) { return NC_EBADNAME; });
}

}  // namespace

std::expected<DimInfo, NcError> Dataset::dim_info(int dimid, NcOp op) const {
  return id(op, {}).and_then([&](int ncid) -> std::expected<DimInfo, NcError> {
    std::array<char, NC_MAX_NAME + 1> name{};
    std::size_t length = 0;
    if (auto done = nc_call(
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

std::expected<VarInfo, NcError> Dataset::var_info(int varid, NcOp op) const {
  const std::expected<int, NcError> ncid = id(op, {});
  if (not ncid) {
    return std::unexpected{ncid.error()};
  }
  std::array<char, NC_MAX_NAME + 1> name{};
  nc_type xtype = NC_NAT;
  int ndims = 0;
  if (auto done = nc_call(op, {}, path_,
                          [&] {
                            return nc_inq_var(*ncid, varid, name.data(), &xtype,
                                              &ndims, nullptr, nullptr);
                          });
      not done) {
    return std::unexpected{done.error()};
  }
  std::vector<int> dimids(static_cast<std::size_t>(ndims));
  if (auto done =
          nc_call(op, name.data(), path_,
                  [&] { return nc_inq_vardimid(*ncid, varid, dimids.data()); });
      not done) {
    return std::unexpected{done.error()};
  }
  auto dims = io::detail::traverse(
      dimids, [&](int dimid) { return dim_info(dimid, op); });
  if (not dims) {
    return std::unexpected{dims.error()};
  }
  auto valid = name_from(name.data());
  if (not valid) {
    return std::unexpected{fail(LibraryStatus{valid.error()}, op, name.data())};
  }
  return VarInfo{.id = varid,
                 .name = *std::move(valid),
                 .type = to_type(xtype),
                 .dims = *std::move(dims)};
}

std::expected<VarInfo, NcError> Dataset::var(NcNameRef name, NcOp op) const {
  return id(op, name.view()).and_then([&](int ncid) {
    int varid = 0;
    return nc_call(op, name.view(), path_,
                   [&] { return nc_inq_varid(ncid, name.c_str(), &varid); })
        .and_then([&] { return var_info(varid, op); });
  });
}

std::expected<int, NcError> Dataset::att_owner(AttTarget on, NcNameRef name,
                                               NcOp op) const {
  return id(op, {}).and_then([&](int ncid) -> std::expected<int, NcError> {
    const std::optional<NcNameRef> variable = on.variable();
    if (not variable) {
      return NC_GLOBAL;
    }
    int varid = 0;
    const int status = nc_status(
        [&] { return nc_inq_varid(ncid, variable->c_str(), &varid); });
    if (status != NC_NOERR) {
      return std::unexpected{att_fail(LibraryStatus{status}, op, on, name)};
    }
    return varid;
  });
}

std::expected<std::optional<Dataset::AttShape>, NcError> Dataset::att_shape(
    int varid, AttTarget on, NcNameRef name) const {
  using Result = std::expected<std::optional<AttShape>, NcError>;
  return id(NcOp::get_att, {}).and_then([&](int ncid) -> Result {
    nc_type xtype = NC_NAT;
    std::size_t length = 0;
    const int status = nc_status(
        [&] { return nc_inq_att(ncid, varid, name.c_str(), &xtype, &length); });
    if (status == NC_ENOTATT) {
      return std::nullopt;
    }
    if (status != NC_NOERR) {
      return std::unexpected{
          att_fail(LibraryStatus{status}, NcOp::get_att, on, name)};
    }
    return AttShape{.type = to_type(xtype), .length = length};
  });
}

}  // namespace detail

// ---- File -------------------------------------------------------------------

namespace {

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

std::expected<File, NcError> File::open(const std::filesystem::path& path,
                                        const ReadLimits& limits) {
  // Our own copy: the caller's path cannot change under the call.
  std::filesystem::path owned = path;
  const auto error = [&owned](NcStatus status) {
    return std::unexpected{NcError{
        .status = status, .op = NcOp::open, .object = {}, .file = owned}};
  };
  if (const std::optional<int> refused = not_a_regular_file(owned)) {
    return error(LibraryStatus{*refused});
  }
  const auto native = detail::nc_path(owned);
  if (not native) {
    return error(native.error());
  }
  int ncid = 0;
  if (const int status = detail::nc_status(
          [&] { return nc_open(native->c_str(), NC_NOWRITE, &ncid); });
      status != NC_NOERR) {
    return error(LibraryStatus{status});
  }
  return File{ncid, std::move(owned), limits};
}

File& File::operator=(File&& other) noexcept {
  if (this != &other) {
    drop();
    Dataset::operator=(std::move(other));
  }
  return *this;
}

File::~File() { drop(); }

void File::drop() noexcept {
  if (not ncid_) {
    return;
  }
  const int ncid = *ncid_;
  forget();  // whatever nc_close returns: see close()
  static_cast<void>(detail::nc_status([ncid] { return nc_close(ncid); }));
}

std::expected<void, NcError> File::close() && {
  const std::expected<int, NcError> ncid = id(NcOp::close, {});
  if (not ncid) {
    return std::unexpected{ncid.error()};
  }
  // Given up whatever nc_close returns: after a failed close netCDF-C may
  // have freed part of the file's state, and nc_abort would free it again.
  forget();
  return detail::nc_call(NcOp::close, {}, path_,
                         [ncid = *ncid] { return nc_close(ncid); });
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
    return io::detail::traverse(
        varids, [&](int varid) { return var_info(varid, NcOp::inquire); });
  });
}

}  // namespace mov::io::nc
