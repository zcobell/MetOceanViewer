// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The raw netCDF-C handle the fixture generators write with: independent of
// mov::io, so a bug in the wrapper cannot hide in the files that test it.

#pragma once

#include <netcdf.h>

#include <cstddef>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mov::test::ncgen {

inline void check(int status, std::string_view what) {
  if (status != NC_NOERR) {
    throw std::runtime_error{
        std::format("netCDF fixture: {}: {}", what, nc_strerror(status))};
  }
}

// A raw netCDF-C file, closed (and checked) by close() or the destructor.
class Raw {
 public:
  explicit Raw(const std::filesystem::path& path, int cmode = NC_NETCDF4) {
    check(nc_create(path.string().c_str(), cmode | NC_CLOBBER, &ncid_),
          "nc_create");
  }
  struct Open {};
  Raw(const std::filesystem::path& path, Open /*unused*/) {
    check(nc_open(path.string().c_str(), NC_NOWRITE, &ncid_), "nc_open");
  }
  Raw(const Raw&) = delete;
  Raw& operator=(const Raw&) = delete;
  Raw(Raw&&) = delete;
  Raw& operator=(Raw&&) = delete;
  ~Raw() {
    if (ncid_ >= 0) {
      nc_close(ncid_);
    }
  }
  void close() {
    const int status = nc_close(ncid_);
    ncid_ = -1;
    check(status, "nc_close");
  }

  [[nodiscard]] int id() const { return ncid_; }

  int dim(const char* name, std::size_t length) const {
    int dimid = 0;
    check(nc_def_dim(ncid_, name, length, &dimid), name);
    return dimid;
  }
  int var(const char* name, nc_type type, std::vector<int> dims) const {
    int varid = 0;
    check(nc_def_var(ncid_, name, type, static_cast<int>(dims.size()),
                     dims.data(), &varid),
          name);
    return varid;
  }
  [[nodiscard]] int varid(std::string_view name) const {
    int varid = NC_GLOBAL;
    if (not name.empty()) {
      check(nc_inq_varid(ncid_, std::string{name}.c_str(), &varid),
            "nc_inq_varid");
    }
    return varid;
  }
  void text(int varid, const char* name, std::string_view value) const {
    check(nc_put_att_text(ncid_, varid, name, value.size(), value.data()),
          name);
  }
  void enddef() const { check(nc_enddef(ncid_), "nc_enddef"); }

 private:
  int ncid_{-1};
};

}  // namespace mov::test::ncgen
