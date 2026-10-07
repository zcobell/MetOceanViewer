// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// netCDF fixtures written at test time with the raw netCDF-C API (plus one
// hand-made classic file the API refuses to write), independent of mov::io,
// so a bug in the wrapper cannot hide in the fixture that tests it. Each
// function writes one file and throws std::runtime_error, naming the call,
// on any netCDF-C error. The contents are listed beside each function; the
// tests rely on them.

#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mov::test::ncgen {

/// Dimension n = 4 and, each over (n) unless noted:
///   v_double {1.5, -2.25, 3, 4}       v_float  {1.5f, -2.25f, 3, 4}
///   v_byte   {1, -2, 3, 4}            v_short  {1, -2, 300, 4}
///   v_int    {1, -2, 70000, 4}        v_int64  {1, -2, 2^40, 4}
///   v_ubyte, v_ushort, v_uint, v_uint64 {1, 2, 3, 4}
///   v_char(n, len3) rows "abc" "de\0" "fgh" "\0\0\0"
///   v_string        {"one", "two", "three", "four"}
///   v_opaque        a user-defined (opaque) type
///   s_double        scalar 7.5
///   grid(rows = 3, cols = 4) double, r * 10 + c
///   cube(a = 2, b = 3, c = 4) int, a * 100 + b * 10 + c
void make_typed(const std::filesystem::path& path);

/// Dimension n = 6; the missing-data cases (B4, B9, B12):
///   d_fill    double, _FillValue -99999: {1, -99999, 2, NaN, 3, 4}
///   f_fill    float,  _FillValue -99999f: {1, -99999, 2, NaN, 3, 4}
///   d_default double, no _FillValue, only [0, 3) written: {1, 2, 3, fill...}
///   f_default float, as d_default
///   b_default byte, as d_default: {1, 2, 3, -127 (NC_FILL_BYTE) ...}
///   d_nofill  double, NC_NOFILL: {1, 2, 3, 9.9692099683868690e36 (the
///             default fill value, written as data), 5, 6}
///   f_missing float, missing_value {-999f, -888f}: {1, -999, -888, 2, 3, 4}
///   d_missing1 double, missing_value -1 (scalar): {-1, 0, 1, 2, 3, 4}
///   s_packed  short, _FillValue -32767, scale_factor 0.5 (float), add_offset
///             10 (double): {0, 2, -32767, 4, 6, 8}
///   d_valid   double, valid_min 0, valid_max 10: {-1, 0, 5, 10, 11, 3}
///   d_range   double, valid_range {0, 10}: as d_valid
///   d_fill999 double, _FillValue -999: {-999, -999.0000001, -5, 1, 2, 3}
///   f_fill5   float, _FillValue -5f: {-5, -999, 1, 2, 3, 4}
///   b_unsigned byte, _Unsigned "true"
///   i_int64   int64 {1, 2, 3, 4, 5, 6}
///   d_badscale double, scale_factor of type char ("2")
///   d_badrange double, valid_range of one value
///   d_badmissing float, missing_value double 0.1 (no float equals it)
///   f_missing_d float, missing_value double -999 (netCDF4-python style):
///             {1, -999, 2, 3, 4, 5}
///   i_missing_64 int, missing_value int64 -999: {-999, 1, 2, 3, 4, 5}
///   i_badmax  int, valid_max int64 2^40 (no int equals it)
///   d_intscale double, scale_factor int 2: {1, 2, 3, 4, 5, 6}
void make_masking(const std::filesystem::path& path);

/// Dimensions station = rows.size(), name_len = `name_len`; char
/// station_name(station, name_len) holding each row's bytes NUL-padded
/// (a row may hold junk after a NUL; none may be longer than name_len).
void make_char_rows(const std::filesystem::path& path, std::size_t name_len,
                    std::span<const std::string> rows);

/// A global NC_CHAR attribute `history` of exactly `bytes` bytes ('a'..'z'
/// repeated), and on variable `x`(dim n = 1) the attributes:
///   HorizontalProjectionEPSG = 4326 (int)          (on x)
///   units = "degrees_east" (char)                  (on x)
///   title = "Genève ✓" (char, UTF-8: 11 bytes)    (global)
///   empty = "" (char, 0 bytes)                     (global)
///   s_title = "hello" (NC_STRING, one)             (global)
///   s_pair = {"a", "b"} (NC_STRING, two)           (global)
///   doubles = {1.5, 2.5, 3.5} (double)             (global)
///   flags = {1, 2} (ubyte)                         (global)
/// and variable `y`(n) with HorizontalProjectionEPSG = "4326" (char, B10).
void make_attributes(const std::filesystem::path& path, std::size_t bytes);

/// char one(n = 5) "hello"; three(a = 2, b = 3, c = 2) "abcdefghijkl";
/// scalar (rank 0) 'x'.
void make_char_shapes(const std::filesystem::path& path);

/// A 2-D double variable `data`(rows, cols) holding row * cols + col.
void make_matrix(const std::filesystem::path& path, std::size_t rows,
                 std::size_t cols);

/// The hostile-structure set (design review S11). Each file holds what its
/// name says; the tests check each gives a specific error and no oversized
/// allocation.
enum class Hostile {
  huge_dim,         // dims big = 2^40, wide = 2^30; chunked double vars
                    // `empty`(big) and `square`(big, big, wide), no data
  big_attribute,    // global char `history` of (1 MiB + 1) bytes
  string_null,      // NC_STRING var `ids`(n = 3) {"a", NULL, "ccc"}
  fill_wrong_type,  // classic file: double `v`(n = 2) with a float _FillValue
  fill_two_values,  // classic file: double `v`(n = 2) with _FillValue {1, 2}
  zero_length_name_len,  // station = 2, unlimited name_len of length 0, char
                         // station_name(station, name_len)
  fill_valued_time,      // double time(time = 3), _FillValue -1, all -1
  time_not_first,        // dims station = 2 (id 0), time = 3 (id 1); vars
                         // lat(station) (id 0), time(time) (id 1) {0, 60, 120}
  epsg_as_text,          // x(n = 1) with HorizontalProjectionEPSG = "4326"
};
void make_hostile(const std::filesystem::path& path, Hostile kind);

/// A file whose bytes are not netCDF.
void make_not_netcdf(const std::filesystem::path& path);

/// Closes the HDF5 dataset `name` ("/v") that netCDF-C holds open, behind
/// its back, so its next nc_close fails while it releases the file. False
/// when no such dataset is open.
[[nodiscard]] bool sabotage_hdf5_dataset(std::string_view name);

// ---- raw queries, for checking what the wrapper wrote ---------------------

struct RawAtt {
  int xtype;
  std::size_t length;
  std::string bytes;  // for NC_CHAR
};
/// The attribute `name` of variable `var` ("" for a global one).
[[nodiscard]] RawAtt raw_att(const std::filesystem::path& path,
                             std::string_view var, std::string_view name);
[[nodiscard]] int raw_format(const std::filesystem::path& path);
[[nodiscard]] int raw_var_type(const std::filesystem::path& path,
                               std::string_view var);
[[nodiscard]] std::vector<double> raw_doubles(const std::filesystem::path& path,
                                              std::string_view var);
[[nodiscard]] std::string raw_chars(const std::filesystem::path& path,
                                    std::string_view var);
/// _FillValue of a double variable as nc_inq_var_fill reports it.
[[nodiscard]] double raw_double_fill(const std::filesystem::path& path,
                                     std::string_view var);
/// shuffle, deflate and level; chunk sizes (empty when contiguous).
struct RawStorage {
  int shuffle;
  int deflate;
  int level;
  std::vector<std::size_t> chunks;
};
[[nodiscard]] RawStorage raw_storage(const std::filesystem::path& path,
                                     std::string_view var);

}  // namespace mov::test::ncgen
