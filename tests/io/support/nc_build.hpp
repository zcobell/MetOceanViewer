// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// A small builder over the raw netCDF-C API for the station netCDF fixtures
// (legacy, foreign CF and hostile files): independent of mov::io, so a bug in
// the reader cannot hide in the file that tests it. Definitions come first;
// the first data call ends define mode, and a later definition re-enters it.
// Every call throws std::runtime_error, naming the call, on a netCDF-C error.
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

class Cdf {
 public:
  explicit Cdf(const std::filesystem::path& path, int cmode = NC_NETCDF4)
      : file_{path, cmode} {}

  /// A dimension; length 0 is the unlimited dimension.
  int dim(std::string_view name, std::size_t length) {
    redef();
    int id = 0;
    check(nc_def_dim(ncid(), std::string{name}.c_str(), length, &id),
          std::string{name});
    return id;
  }

  /// A variable over `dims`; returns its name for chaining.
  std::string var(std::string_view name, nc_type type,
                  std::initializer_list<int> dims = {}) {
    redef();
    const std::vector<int> ids{dims};
    int id = 0;
    check(nc_def_var(ncid(), std::string{name}.c_str(), type,
                     static_cast<int>(ids.size()), ids.data(), &id),
          std::string{name});
    return std::string{name};
  }

  /// A chunked variable (so a huge dimension costs nothing until written).
  std::string chunked(std::string_view name, nc_type type,
                      std::initializer_list<int> dims,
                      std::vector<std::size_t> chunks) {
    const std::string n = var(name, type, dims);
    check(nc_def_var_chunking(ncid(), varid(n), NC_CHUNKED, chunks.data()), n);
    return n;
  }

  // ---- attributes ("" is the file) -----------------------------------------

  Cdf& text(std::string_view var_name, const char* att,
            std::string_view value) {
    redef();
    check(nc_put_att_text(ncid(), varid(var_name), att, value.size(),
                          value.data()),
          att);
    return *this;
  }

  /// A one-element NC_STRING attribute.
  Cdf& string_att(std::string_view var_name, const char* att,
                  const std::string& value) {
    redef();
    const char* p = value.c_str();
    check(nc_put_att_string(ncid(), varid(var_name), att, 1, &p), att);
    return *this;
  }

  /// A numeric attribute of the given external type.
  Cdf& num(std::string_view var_name, const char* att, nc_type type,
           std::initializer_list<double> values) {
    redef();
    check(nc_put_att_double(ncid(), varid(var_name), att, type, values.size(),
                            std::data(values)),
          att);
    return *this;
  }

  Cdf& int64_att(std::string_view var_name, const char* att,
                 std::initializer_list<long long> values) {
    redef();
    check(nc_put_att_longlong(ncid(), varid(var_name), att, NC_INT64,
                              values.size(), std::data(values)),
          att);
    return *this;
  }

  Cdf& del_att(std::string_view var_name, const char* att) {
    redef();
    check(nc_del_att(ncid(), varid(var_name), att), att);
    return *this;
  }

  // ---- data -----------------------------------------------------------------

  /// All of a numeric variable (netCDF converts from double).
  void put(std::string_view name, std::span<const double> values) {
    data();
    check(nc_put_var_double(ncid(), varid(name), values.data()),
          std::string{name});
  }

  void put_i64(std::string_view name, std::span<const std::int64_t> values) {
    data();
    check(
        nc_put_var_longlong(ncid(), varid(name),
                            reinterpret_cast<const long long*>(values.data())),
        std::string{name});
  }

  /// `count` elements from `start` of a 1-D numeric variable.
  void put_range(std::string_view name, std::size_t start,
                 std::span<const double> values) {
    data();
    const std::size_t count = values.size();
    check(
        nc_put_vara_double(ncid(), varid(name), &start, &count, values.data()),
        std::string{name});
  }

  void put_i64_range(std::string_view name, std::size_t start,
                     std::span<const std::int64_t> values) {
    data();
    const std::size_t count = values.size();
    check(
        nc_put_vara_longlong(ncid(), varid(name), &start, &count,
                             reinterpret_cast<const long long*>(values.data())),
        std::string{name});
  }

  /// A block of a 2-D numeric variable: `rows` x `cols` values at
  /// (row, col), row-major.
  void put_block(std::string_view name, std::size_t row, std::size_t col,
                 std::size_t rows, std::size_t cols,
                 std::span<const double> values) {
    data();
    const std::size_t start[2]{row, col};    // NOLINT(modernize-avoid-c-arrays)
    const std::size_t count[2]{rows, cols};  // NOLINT(modernize-avoid-c-arrays)
    check(nc_put_vara_double(ncid(), varid(name), start, count, values.data()),
          std::string{name});
  }

  /// One element of a numeric variable.
  void put1(std::string_view name, std::initializer_list<std::size_t> index,
            double value) {
    data();
    check(nc_put_var1_double(ncid(), varid(name), std::data(index), &value),
          std::string{name});
  }

  /// The rows of a 2-D char variable, NUL-padded to `width`; a row may hold
  /// NULs and junk (it must not be longer than `width`).
  void put_rows(std::string_view name, std::size_t width,
                const std::vector<std::string>& rows) {
    data();
    for (std::size_t i = 0; i < rows.size(); ++i) {
      if (rows[i].size() > width) {
        throw std::runtime_error{
            "netCDF fixture: a row is longer than its dim"};
      }
      std::string padded = rows[i];
      padded.resize(width, '\0');
      const std::size_t start[2]{i, 0};      // NOLINT(modernize-avoid-c-arrays)
      const std::size_t count[2]{1, width};  // NOLINT(modernize-avoid-c-arrays)
      if (width > 0) {
        check(
            nc_put_vara_text(ncid(), varid(name), start, count, padded.data()),
            std::string{name});
      }
    }
  }

  /// A 1-D char variable (a scalar station's name).
  void put_text(std::string_view name, std::string_view bytes) {
    data();
    check(nc_put_var_text(ncid(), varid(name), bytes.data()),
          std::string{name});
  }

  /// An NC_STRING variable; an element may be nullptr-like by passing nullopt
  /// in `has_value` form: use `nullptr_at` to mark NULL elements.
  void put_strings(std::string_view name, const std::vector<std::string>& v,
                   const std::vector<bool>& is_null = {}) {
    data();
    std::vector<const char*> ptrs;
    for (std::size_t i = 0; i < v.size(); ++i) {
      ptrs.push_back(i < is_null.size() and is_null[i] ? nullptr
                                                       : v[i].c_str());
    }
    check(nc_put_var_string(ncid(), varid(name), ptrs.data()),
          std::string{name});
  }

  void close() { file_.close(); }

  [[nodiscard]] int ncid() const { return file_.id(); }

 private:
  [[nodiscard]] int varid(std::string_view name) const {
    int id = NC_GLOBAL;
    if (not name.empty()) {
      check(nc_inq_varid(ncid(), std::string{name}.c_str(), &id),
            std::string{name});
    }
    return id;
  }
  void redef() {
    if (data_mode_) {
      check(nc_redef(ncid()), "nc_redef");
      data_mode_ = false;
    }
  }
  void data() {
    if (not data_mode_) {
      check(nc_enddef(ncid()), "nc_enddef");
      data_mode_ = true;
    }
  }

  Raw file_;
  bool data_mode_{false};
};

}  // namespace mov::test::ncgen
