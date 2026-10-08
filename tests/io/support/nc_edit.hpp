// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Edits an existing netCDF file with the raw netCDF-C API: how the station
// netCDF validation tests turn a valid file into one that breaks exactly one
// rule, independent of mov::io. Every call throws std::runtime_error on a
// netCDF-C error; the destructor closes the file.

#pragma once

#include <netcdf.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "nc_raw.hpp"

namespace mov::test::ncgen {

class Editor {
 public:
  explicit Editor(const std::filesystem::path& path) {
    check(nc_open(path.string().c_str(), NC_WRITE, &ncid_), "nc_open");
  }
  Editor(const Editor&) = delete;
  Editor& operator=(const Editor&) = delete;
  Editor(Editor&&) = delete;
  Editor& operator=(Editor&&) = delete;
  ~Editor() { nc_close(ncid_); }

  /// The variable's id; "" is NC_GLOBAL.
  [[nodiscard]] int varid(std::string_view name) const {
    int id = NC_GLOBAL;
    if (not name.empty()) {
      check(nc_inq_varid(ncid_, std::string{name}.c_str(), &id), name);
    }
    return id;
  }

  void text(std::string_view var, const char* att, std::string_view value) {
    redef();
    check(nc_put_att_text(ncid_, varid(var), att, value.size(), value.data()),
          att);
  }
  void doubles(std::string_view var, const char* att,
               std::initializer_list<double> values) {
    redef();
    check(nc_put_att_double(ncid_, varid(var), att, NC_DOUBLE, values.size(),
                            std::data(values)),
          att);
  }
  void ints(std::string_view var, const char* att,
            std::initializer_list<int> values) {
    redef();
    check(nc_put_att_int(ncid_, varid(var), att, NC_INT, values.size(),
                         std::data(values)),
          att);
  }
  void remove_att(std::string_view var, const char* att) {
    redef();
    check(nc_del_att(ncid_, varid(var), att), att);
  }
  /// A new variable over the named dimensions (no data written: it reads as
  /// its fill).
  void add_var(const char* name, nc_type type,
               std::initializer_list<const char*> dims) {
    redef();
    std::vector<int> ids;
    for (const char* dim : dims) {
      int dimid = 0;
      check(nc_inq_dimid(ncid_, dim, &dimid), dim);
      ids.push_back(dimid);
    }
    int id = 0;
    check(nc_def_var(ncid_, name, type, static_cast<int>(ids.size()),
                     ids.data(), &id),
          name);
  }

  void put(std::string_view var, std::initializer_list<std::size_t> index,
           double value) {
    enddef();
    check(nc_put_var1_double(ncid_, varid(var), std::data(index), &value), var);
  }
  void put_int(std::string_view var, std::initializer_list<std::size_t> index,
               int value) {
    enddef();
    check(nc_put_var1_int(ncid_, varid(var), std::data(index), &value), var);
  }
  void put_byte(std::string_view var, std::initializer_list<std::size_t> index,
                signed char value) {
    enddef();
    check(nc_put_var1_schar(ncid_, varid(var), std::data(index), &value), var);
  }
  /// Overwrites row `row` of a 2-D char variable from its first column with
  /// `bytes` (which may hold NULs and need not fill the row).
  void put_chars(std::string_view var, std::size_t row,
                 std::string_view bytes) {
    enddef();
    const std::size_t start[2]{row, 0};  // NOLINT(modernize-avoid-c-arrays)
    const std::size_t count[2]{1, bytes.size()};  // NOLINT
    check(nc_put_vara_text(ncid_, varid(var), start, count, bytes.data()), var);
  }

 private:
  void redef() {
    if (not define_) {
      check(nc_redef(ncid_), "nc_redef");
      define_ = true;
    }
  }
  void enddef() {
    if (define_) {
      check(nc_enddef(ncid_), "nc_enddef");
      define_ = false;
    }
  }

  int ncid_{-1};
  bool define_{false};
};

}  // namespace mov::test::ncgen
