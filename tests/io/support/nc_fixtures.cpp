// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "nc_fixtures.hpp"

#include <netcdf.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mov::test::ncgen {

namespace {

void check(int status, std::string_view what) {
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

template <class T>
void put_all(const Raw& f, int varid, const std::vector<T>& v) {
  if constexpr (std::same_as<T, double>) {
    check(nc_put_var_double(f.id(), varid, v.data()), "put double");
  } else if constexpr (std::same_as<T, float>) {
    check(nc_put_var_float(f.id(), varid, v.data()), "put float");
  } else if constexpr (std::same_as<T, signed char>) {
    check(nc_put_var_schar(f.id(), varid, v.data()), "put schar");
  } else if constexpr (std::same_as<T, short>) {
    check(nc_put_var_short(f.id(), varid, v.data()), "put short");
  } else if constexpr (std::same_as<T, int>) {
    check(nc_put_var_int(f.id(), varid, v.data()), "put int");
  } else if constexpr (std::same_as<T, long long>) {
    check(nc_put_var_longlong(f.id(), varid, v.data()), "put longlong");
  } else {
    static_assert(std::same_as<T, unsigned int>);
    check(nc_put_var_uint(f.id(), varid, v.data()), "put uint");
  }
}

void put_head(const Raw& f, int varid, const std::vector<double>& v) {
  const std::array<std::size_t, 1> start{0};
  const std::array<std::size_t, 1> count{v.size()};
  check(nc_put_vara_double(f.id(), varid, start.data(), count.data(), v.data()),
        "put head");
}

void define_typed_numbers(const Raw& f, int n) {
  put_all(f, f.var("v_double", NC_DOUBLE, {n}),
          std::vector<double>{1.5, -2.25, 3, 4});
  put_all(f, f.var("v_float", NC_FLOAT, {n}),
          std::vector<float>{1.5F, -2.25F, 3, 4});
  put_all(f, f.var("v_byte", NC_BYTE, {n}),
          std::vector<signed char>{1, -2, 3, 4});
  put_all(f, f.var("v_short", NC_SHORT, {n}),
          std::vector<short>{1, -2, 300, 4});
  put_all(f, f.var("v_int", NC_INT, {n}), std::vector<int>{1, -2, 70000, 4});
  put_all(f, f.var("v_int64", NC_INT64, {n}),
          std::vector<long long>{1, -2, 1LL << 40, 4});
  for (const auto& [name, type] :
       std::array<std::pair<const char*, nc_type>, 4>{
           {{"v_ubyte", NC_UBYTE},
            {"v_ushort", NC_USHORT},
            {"v_uint", NC_UINT},
            {"v_uint64", NC_UINT64}}}) {
    put_all(f, f.var(name, type, {n}), std::vector<unsigned int>{1, 2, 3, 4});
  }
  put_all(f, f.var("s_double", NC_DOUBLE, {}), std::vector<double>{7.5});
}

void define_typed_text(const Raw& f, int n) {
  const int len3 = f.dim("len3", 3);
  const int chars = f.var("v_char", NC_CHAR, {n, len3});
  using namespace std::string_literals;
  const std::string bytes = "abcde\0fgh\0\0\0"s;
  check(nc_put_var_text(f.id(), chars, bytes.data()), "v_char");
  const int strings = f.var("v_string", NC_STRING, {n});
  std::array<const char*, 4> values{"one", "two", "three", "four"};
  check(nc_put_var_string(f.id(), strings, values.data()), "v_string");
  nc_type opaque = 0;
  check(nc_def_opaque(f.id(), 4, "blob", &opaque), "nc_def_opaque");
  static_cast<void>(f.var("v_opaque", opaque, {n}));
}

void define_typed_arrays(const Raw& f) {
  const int rows = f.dim("rows", 3);
  const int cols = f.dim("cols", 4);
  std::vector<double> grid;
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 4; ++c) {
      grid.push_back(r * 10 + c);
    }
  }
  put_all(f, f.var("grid", NC_DOUBLE, {rows, cols}), grid);
  const int a = f.dim("a", 2);
  const int b = f.dim("b", 3);
  const int c = f.dim("c", 4);
  std::vector<int> cube;
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 3; ++j) {
      for (int k = 0; k < 4; ++k) {
        cube.push_back(i * 100 + j * 10 + k);
      }
    }
  }
  put_all(f, f.var("cube", NC_INT, {a, b, c}), cube);
}

template <class T>
void att(const Raw& f, int varid, const char* name, nc_type type,
         std::vector<T> values) {
  if constexpr (std::same_as<T, double>) {
    check(nc_put_att_double(f.id(), varid, name, type, values.size(),
                            values.data()),
          name);
  } else if constexpr (std::same_as<T, float>) {
    check(nc_put_att_float(f.id(), varid, name, type, values.size(),
                           values.data()),
          name);
  } else if constexpr (std::same_as<T, short>) {
    check(nc_put_att_short(f.id(), varid, name, type, values.size(),
                           values.data()),
          name);
  } else {
    static_assert(std::same_as<T, int>);
    check(
        nc_put_att_int(f.id(), varid, name, type, values.size(), values.data()),
        name);
  }
}

void nofill(const Raw& f, int varid) {
  check(nc_def_var_fill(f.id(), varid, NC_NOFILL, nullptr), "NC_NOFILL");
}

constexpr double nan = std::numeric_limits<double>::quiet_NaN();
constexpr float nanf = std::numeric_limits<float>::quiet_NaN();

void define_fill_cases(const Raw& f, int n) {
  const int d_fill = f.var("d_fill", NC_DOUBLE, {n});
  att(f, d_fill, "_FillValue", NC_DOUBLE, std::vector<double>{-99999});
  put_all(f, d_fill, std::vector<double>{1, -99999, 2, nan, 3, 4});
  const int f_fill = f.var("f_fill", NC_FLOAT, {n});
  att(f, f_fill, "_FillValue", NC_FLOAT, std::vector<float>{-99999});
  put_all(f, f_fill, std::vector<float>{1, -99999, 2, nanf, 3, 4});
  // Only the head is written; the rest keeps the library's default fill.
  put_head(f, f.var("d_default", NC_DOUBLE, {n}), {1, 2, 3});
  put_head(f, f.var("f_default", NC_FLOAT, {n}), {1, 2, 3});
  put_head(f, f.var("b_default", NC_BYTE, {n}), {1, 2, 3});
  const int d_nofill = f.var("d_nofill", NC_DOUBLE, {n});
  nofill(f, d_nofill);
  put_all(f, d_nofill, std::vector<double>{1, 2, 3, NC_FILL_DOUBLE, 5, 6});
  const int d_fill999 = f.var("d_fill999", NC_DOUBLE, {n});
  att(f, d_fill999, "_FillValue", NC_DOUBLE, std::vector<double>{-999});
  put_all(f, d_fill999, std::vector<double>{-999, -999.0000001, -5, 1, 2, 3});
  const int f_fill5 = f.var("f_fill5", NC_FLOAT, {n});
  att(f, f_fill5, "_FillValue", NC_FLOAT, std::vector<float>{-5});
  put_all(f, f_fill5, std::vector<float>{-5, -999, 1, 2, 3, 4});
}

void define_missing_cases(const Raw& f, int n) {
  const int f_missing = f.var("f_missing", NC_FLOAT, {n});
  att(f, f_missing, "missing_value", NC_FLOAT, std::vector<float>{-999, -888});
  put_all(f, f_missing, std::vector<float>{1, -999, -888, 2, 3, 4});
  const int d_missing1 = f.var("d_missing1", NC_DOUBLE, {n});
  att(f, d_missing1, "missing_value", NC_DOUBLE, std::vector<double>{-1});
  put_all(f, d_missing1, std::vector<double>{-1, 0, 1, 2, 3, 4});
  const int s_packed = f.var("s_packed", NC_SHORT, {n});
  att(f, s_packed, "_FillValue", NC_SHORT, std::vector<short>{-32767});
  att(f, s_packed, "scale_factor", NC_FLOAT, std::vector<float>{0.5F});
  att(f, s_packed, "add_offset", NC_DOUBLE, std::vector<double>{10});
  put_all(f, s_packed, std::vector<short>{0, 2, -32767, 4, 6, 8});
  const std::vector<double> valid{-1, 0, 5, 10, 11, 3};
  const int d_valid = f.var("d_valid", NC_DOUBLE, {n});
  att(f, d_valid, "valid_min", NC_DOUBLE, std::vector<double>{0});
  att(f, d_valid, "valid_max", NC_DOUBLE, std::vector<double>{10});
  put_all(f, d_valid, valid);
  const int d_range = f.var("d_range", NC_DOUBLE, {n});
  att(f, d_range, "valid_range", NC_DOUBLE, std::vector<double>{0, 10});
  put_all(f, d_range, valid);
}

void define_refused_cases(const Raw& f, int n) {
  const int b_unsigned = f.var("b_unsigned", NC_BYTE, {n});
  f.text(b_unsigned, "_Unsigned", "true");
  put_head(f, b_unsigned, {1, 2});
  put_all(f, f.var("i_int64", NC_INT64, {n}),
          std::vector<long long>{1, 2, 3, 4, 5, 6});
  const int d_badscale = f.var("d_badscale", NC_DOUBLE, {n});
  att(f, d_badscale, "scale_factor", NC_INT, std::vector<int>{2});
  const int d_badrange = f.var("d_badrange", NC_DOUBLE, {n});
  att(f, d_badrange, "valid_range", NC_DOUBLE, std::vector<double>{0});
  const int d_badmissing = f.var("d_badmissing", NC_DOUBLE, {n});
  att(f, d_badmissing, "missing_value", NC_FLOAT, std::vector<float>{-1});
}

// ---- a classic (CDF-1) file written byte by byte ---------------------------
// netCDF-C refuses to write a _FillValue whose type differs from its
// variable's, or that has more than one value, but such files exist (other
// writers), so the header is assembled here.

class Cdf1 {
 public:
  void int32(std::int32_t v) {
    const auto u = std::bit_cast<std::uint32_t>(v);
    for (int shift = 24; shift >= 0; shift -= 8) {
      bytes_.push_back(
          static_cast<char>((u >> static_cast<unsigned>(shift)) & 0xFFU));
    }
  }
  void name(std::string_view s) {
    int32(static_cast<std::int32_t>(s.size()));
    bytes_.append(s);
    pad();
  }
  void float32(float v) { int32(std::bit_cast<std::int32_t>(v)); }
  void float64(double v) {
    const auto u = std::bit_cast<std::uint64_t>(v);
    int32(static_cast<std::int32_t>(static_cast<std::uint32_t>(u >> 32U)));
    int32(static_cast<std::int32_t>(static_cast<std::uint32_t>(u)));
  }
  void raw(std::string_view s) { bytes_.append(s); }
  void pad() {
    while (bytes_.size() % 4 != 0) {
      bytes_.push_back('\0');
    }
  }
  [[nodiscard]] std::size_t size() const { return bytes_.size(); }
  [[nodiscard]] const std::string& bytes() const { return bytes_; }

 private:
  std::string bytes_;
};

// double v(n = 2) = {1, -1} with the _FillValue attribute `fill` writes.
template <class WriteFill>
void write_classic_fill(const std::filesystem::path& path,
                        const WriteFill& fill) {
  constexpr std::int32_t nc_dimension = 0x0A;
  constexpr std::int32_t nc_variable = 0x0B;
  constexpr std::int32_t nc_attribute = 0x0C;
  Cdf1 header;
  header.raw(std::string_view{"CDF\x01", 4});
  header.int32(0);  // numrecs
  header.int32(nc_dimension);
  header.int32(1);
  header.name("n");
  header.int32(2);
  header.int32(0);  // no global attributes
  header.int32(0);
  header.int32(nc_variable);
  header.int32(1);
  header.name("v");
  header.int32(1);  // rank
  header.int32(0);  // dim id
  header.int32(nc_attribute);
  header.int32(1);
  header.name("_FillValue");
  fill(header);
  header.int32(NC_DOUBLE);
  header.int32(16);  // vsize
  const std::size_t begin = header.size() + 4;
  header.int32(static_cast<std::int32_t>(begin));
  header.float64(1);
  header.float64(-1);
  std::ofstream out{path, std::ios::binary | std::ios::trunc};
  out.write(header.bytes().data(),
            static_cast<std::streamsize>(header.bytes().size()));
}

}  // namespace

void make_typed(const std::filesystem::path& path) {
  Raw f{path};
  const int n = f.dim("n", 4);
  define_typed_numbers(f, n);
  define_typed_text(f, n);
  define_typed_arrays(f);
  f.close();
}

void make_masking(const std::filesystem::path& path) {
  Raw f{path};
  const int n = f.dim("n", 6);
  define_fill_cases(f, n);
  define_missing_cases(f, n);
  define_refused_cases(f, n);
  f.close();
}

void make_char_rows(const std::filesystem::path& path, std::size_t name_len,
                    std::span<const std::string> rows) {
  Raw f{path};
  const int station = f.dim("station", rows.size());
  const int len = f.dim("name_len", name_len);
  const int var = f.var("station_name", NC_CHAR, {station, len});
  std::string bytes(rows.size() * name_len, '\0');
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (rows[i].size() > name_len) {
      throw std::invalid_argument{"make_char_rows: row longer than name_len"};
    }
    rows[i].copy(bytes.data() + (i * name_len), rows[i].size());
  }
  check(nc_put_var_text(f.id(), var, bytes.data()), "station_name");
  f.close();
}

void make_attributes(const std::filesystem::path& path, std::size_t bytes) {
  Raw f{path};
  std::string history(bytes, 'a');
  for (std::size_t i = 0; i < bytes; ++i) {
    history[i] = static_cast<char>('a' + static_cast<int>(i % 26));
  }
  f.text(NC_GLOBAL, "history", history);
  f.text(NC_GLOBAL, "title", "Gen\xc3\xa8ve \xe2\x9c\x93");
  f.text(NC_GLOBAL, "empty", "");
  std::array<const char*, 1> title{"hello"};
  check(nc_put_att_string(f.id(), NC_GLOBAL, "s_title", 1, title.data()),
        "s_title");
  std::array<const char*, 2> pair{"a", "b"};
  check(nc_put_att_string(f.id(), NC_GLOBAL, "s_pair", 2, pair.data()),
        "s_pair");
  att(f, NC_GLOBAL, "doubles", NC_DOUBLE, std::vector<double>{1.5, 2.5, 3.5});
  const std::array<unsigned char, 2> flags{1, 2};
  check(nc_put_att_uchar(f.id(), NC_GLOBAL, "flags", NC_UBYTE, 2, flags.data()),
        "flags");
  const int n = f.dim("n", 1);
  const int x = f.var("x", NC_DOUBLE, {n});
  att(f, x, "HorizontalProjectionEPSG", NC_INT, std::vector<int>{4326});
  f.text(x, "units", "degrees_east");
  const int y = f.var("y", NC_DOUBLE, {n});
  f.text(y, "HorizontalProjectionEPSG", "4326");
  f.close();
}

void make_matrix(const std::filesystem::path& path, std::size_t rows,
                 std::size_t cols) {
  Raw f{path};
  const int r = f.dim("rows", rows);
  const int c = f.dim("cols", cols);
  const int var = f.var("data", NC_DOUBLE, {r, c});
  std::vector<double> values(rows * cols);
  for (std::size_t i = 0; i < values.size(); ++i) {
    values[i] = static_cast<double>(i);
  }
  put_all(f, var, values);
  f.close();
}

namespace {

void make_huge_dim(const std::filesystem::path& path) {
  Raw f{path};
  const int big = f.dim("big", std::size_t{1} << 40U);
  const int var = f.var("empty", NC_DOUBLE, {big});
  const std::array<std::size_t, 1> chunk{1024};
  check(nc_def_var_chunking(f.id(), var, NC_CHUNKED, chunk.data()), "chunks");
  const int wide = f.dim("wide", std::size_t{1} << 30U);
  const int square = f.var("square", NC_DOUBLE, {big, big, wide});
  const std::array<std::size_t, 3> chunks{1, 1, 1024};
  check(nc_def_var_chunking(f.id(), square, NC_CHUNKED, chunks.data()),
        "square chunks");
  f.close();
}

void make_string_null(const std::filesystem::path& path) {
  Raw f{path};
  const int n = f.dim("n", 3);
  const int var = f.var("ids", NC_STRING, {n});
  std::array<const char*, 3> values{"a", nullptr, "ccc"};
  check(nc_put_var_string(f.id(), var, values.data()), "ids");
  f.close();
}

void make_zero_length_name_len(const std::filesystem::path& path) {
  Raw f{path};
  const int station = f.dim("station", 2);
  const int len = f.dim("name_len", NC_UNLIMITED);
  static_cast<void>(f.var("station_name", NC_CHAR, {station, len}));
  f.close();
}

void make_fill_valued_time(const std::filesystem::path& path) {
  Raw f{path};
  const int time = f.dim("time", 3);
  const int var = f.var("time", NC_DOUBLE, {time});
  att(f, var, "_FillValue", NC_DOUBLE, std::vector<double>{-1});
  put_all(f, var, std::vector<double>{-1, -1, -1});
  f.close();
}

void make_time_not_first(const std::filesystem::path& path) {
  Raw f{path};
  const int station = f.dim("station", 2);
  const int time = f.dim("time", 3);
  put_all(f, f.var("lat", NC_DOUBLE, {station}), std::vector<double>{29, 30});
  put_all(f, f.var("time", NC_DOUBLE, {time}), std::vector<double>{0, 60, 120});
  f.close();
}

}  // namespace

void make_hostile(const std::filesystem::path& path, Hostile kind) {
  switch (kind) {
    case Hostile::huge_dim:
      return make_huge_dim(path);
    case Hostile::big_attribute: {
      Raw f{path};
      f.text(NC_GLOBAL, "history",
             std::string((std::size_t{1} << 20U) + 1, 'x'));
      return f.close();
    }
    case Hostile::string_null:
      return make_string_null(path);
    case Hostile::fill_wrong_type:
      return write_classic_fill(path, [](Cdf1& h) {
        h.int32(NC_FLOAT);
        h.int32(1);
        h.float32(-1.0F);
      });
    case Hostile::fill_two_values:
      return write_classic_fill(path, [](Cdf1& h) {
        h.int32(NC_DOUBLE);
        h.int32(2);
        h.float64(1);
        h.float64(2);
      });
    case Hostile::zero_length_name_len:
      return make_zero_length_name_len(path);
    case Hostile::fill_valued_time:
      return make_fill_valued_time(path);
    case Hostile::time_not_first:
      return make_time_not_first(path);
    case Hostile::epsg_as_text: {
      Raw f{path};
      const int x = f.var("x", NC_DOUBLE, {f.dim("n", 1)});
      f.text(x, "HorizontalProjectionEPSG", "4326");
      return f.close();
    }
  }
}

void make_not_netcdf(const std::filesystem::path& path) {
  std::ofstream out{path, std::ios::binary | std::ios::trunc};
  out << "this is not a netCDF file\n";
}

RawAtt raw_att(const std::filesystem::path& path, std::string_view var,
               std::string_view name) {
  const Raw f{path, Raw::Open{}};
  const int varid = f.varid(var);
  const std::string att{name};
  nc_type type = NC_NAT;
  std::size_t length = 0;
  check(nc_inq_att(f.id(), varid, att.c_str(), &type, &length), "nc_inq_att");
  RawAtt result{.xtype = type, .length = length, .bytes = {}};
  if (type == NC_CHAR) {
    result.bytes.resize(length);
    check(nc_get_att_text(f.id(), varid, att.c_str(), result.bytes.data()),
          "nc_get_att_text");
  }
  return result;
}

int raw_format(const std::filesystem::path& path) {
  const Raw f{path, Raw::Open{}};
  int format = 0;
  check(nc_inq_format(f.id(), &format), "nc_inq_format");
  return format;
}

int raw_var_type(const std::filesystem::path& path, std::string_view var) {
  const Raw f{path, Raw::Open{}};
  nc_type type = NC_NAT;
  check(nc_inq_vartype(f.id(), f.varid(var), &type), "nc_inq_vartype");
  return type;
}

std::vector<double> raw_doubles(const std::filesystem::path& path,
                                std::string_view var) {
  const Raw f{path, Raw::Open{}};
  const int varid = f.varid(var);
  int ndims = 0;
  check(nc_inq_varndims(f.id(), varid, &ndims), "nc_inq_varndims");
  std::vector<int> dims(static_cast<std::size_t>(ndims));
  check(nc_inq_vardimid(f.id(), varid, dims.data()), "nc_inq_vardimid");
  std::size_t total = 1;
  for (const int dim : dims) {
    std::size_t length = 0;
    check(nc_inq_dimlen(f.id(), dim, &length), "nc_inq_dimlen");
    total *= length;
  }
  std::vector<double> values(total);
  check(nc_get_var_double(f.id(), varid, values.data()), "nc_get_var_double");
  return values;
}

std::string raw_chars(const std::filesystem::path& path, std::string_view var) {
  const Raw f{path, Raw::Open{}};
  const int varid = f.varid(var);
  std::array<int, 2> dims{};
  check(nc_inq_vardimid(f.id(), varid, dims.data()), "nc_inq_vardimid");
  std::array<std::size_t, 2> lengths{};
  check(nc_inq_dimlen(f.id(), dims[0], lengths.data()), "nc_inq_dimlen");
  check(nc_inq_dimlen(f.id(), dims[1], &lengths[1]), "nc_inq_dimlen");
  std::string bytes(lengths[0] * lengths[1], '\0');
  check(nc_get_var_text(f.id(), varid, bytes.data()), "nc_get_var_text");
  return bytes;
}

double raw_double_fill(const std::filesystem::path& path,
                       std::string_view var) {
  const Raw f{path, Raw::Open{}};
  int no_fill = 0;
  double fill = 0;
  check(nc_inq_var_fill(f.id(), f.varid(var), &no_fill, &fill),
        "nc_inq_var_fill");
  return fill;
}

RawStorage raw_storage(const std::filesystem::path& path,
                       std::string_view var) {
  const Raw f{path, Raw::Open{}};
  const int varid = f.varid(var);
  RawStorage storage{.shuffle = 0, .deflate = 0, .level = 0, .chunks = {}};
  check(nc_inq_var_deflate(f.id(), varid, &storage.shuffle, &storage.deflate,
                           &storage.level),
        "nc_inq_var_deflate");
  int ndims = 0;
  check(nc_inq_varndims(f.id(), varid, &ndims), "nc_inq_varndims");
  int layout = 0;
  std::vector<std::size_t> chunks(static_cast<std::size_t>(ndims));
  check(nc_inq_var_chunking(f.id(), varid, &layout, chunks.data()),
        "nc_inq_var_chunking");
  if (layout == NC_CHUNKED) {
    storage.chunks = chunks;
  }
  return storage;
}

}  // namespace mov::test::ncgen
