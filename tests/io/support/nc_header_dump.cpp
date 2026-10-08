// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "nc_header_dump.hpp"

#include <netcdf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "nc_raw.hpp"

namespace mov::test::ncgen {

namespace {

std::string type_name(nc_type type) {
  switch (type) {
    case NC_BYTE:
      return "byte";
    case NC_CHAR:
      return "char";
    case NC_SHORT:
      return "short";
    case NC_INT:
      return "int";
    case NC_FLOAT:
      return "float";
    case NC_DOUBLE:
      return "double";
    case NC_UBYTE:
      return "ubyte";
    case NC_USHORT:
      return "ushort";
    case NC_UINT:
      return "uint";
    case NC_INT64:
      return "int64";
    case NC_UINT64:
      return "uint64";
    case NC_STRING:
      return "string";
    default:
      throw std::runtime_error{"ncdump_header: user-defined type"};
  }
}

// ncdump's tztrim: trailing zeros after the decimal point are removed, the
// point itself is kept ("6378137." and "1.e+20").
std::string tztrim(std::string s) {
  const std::size_t start = (not s.empty() and s.front() == '-') ? 1 : 0;
  std::size_t end = start;
  while (end < s.size() and
         ((s[end] >= '0' and s[end] <= '9') or s[end] == '.')) {
    ++end;
  }
  if (end == start or s[end - 1] == '.') {
    return s;
  }
  std::size_t keep = end;
  while (keep > start and s[keep - 1] == '0') {
    --keep;
  }
  return s.substr(0, keep) + s.substr(end);
}

// ncdump's default precision: 7 significant digits for floats, 15 for
// doubles, with the decimal point always present ("%#.15g").
template <class F>
std::string floating(F value, int digits, std::string_view suffix) {
  if (std::isnan(value)) {
    return std::format("NaN{}", suffix);
  }
  if (std::isinf(value)) {
    return std::format("{}Infinity{}", value < 0 ? "-" : "", suffix);
  }
  std::array<char, 64> buffer{};
  const int n = std::snprintf(buffer.data(), buffer.size(), "%#.*g", digits,
                              static_cast<double>(value));
  return tztrim(std::string(buffer.data(), static_cast<std::size_t>(n))) +
         std::string{suffix};
}

// ncdump's pr_att_string: trailing NULs dropped, C escapes (a newline is
// printed as \n on the same line by netCDF-C 4.9).
std::string cdl_quoted(std::string_view text) {
  while (not text.empty() and text.back() == '\0') {
    text.remove_suffix(1);
  }
  std::string out = "\"";
  for (const char c : text) {
    const auto uc = static_cast<unsigned char>(c);
    switch (c) {
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\v':
        out += "\\v";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\'':
        out += "\\'";
        break;
      case '"':
        out += "\\\"";
        break;
      default:
        out += (uc < 0x20 or uc == 0x7F) ? std::format("\\{:03o}", uc)
                                         : std::string(1, c);
    }
  }
  return out + "\"";
}

template <class T>
std::vector<T> att_values(int ncid, int varid, const char* name,
                          std::size_t len) {
  std::vector<T> values(len);
  check(nc_get_att(ncid, varid, name, values.data()), name);
  return values;
}

template <class T, class Format>
std::string joined(const std::vector<T>& values, Format format) {
  std::string out;
  for (std::size_t i = 0; i < values.size(); ++i) {
    out += (i == 0 ? "" : ", ") + format(values[i]);
  }
  return out;
}

std::string numeric_values(int ncid, int varid, const char* name, nc_type type,
                           std::size_t len) {
  const auto integer = [](std::string_view suffix) {
    return [suffix](auto x) { return std::format("{}{}", x, suffix); };
  };
  switch (type) {
    case NC_BYTE:
      return joined(att_values<signed char>(ncid, varid, name, len),
                    [](signed char x) { return std::format("{}b", int{x}); });
    case NC_UBYTE:
      return joined(
          att_values<unsigned char>(ncid, varid, name, len),
          [](unsigned char x) { return std::format("{}UB", unsigned{x}); });
    case NC_SHORT:
      return joined(att_values<std::int16_t>(ncid, varid, name, len),
                    integer("s"));
    case NC_USHORT:
      return joined(att_values<std::uint16_t>(ncid, varid, name, len),
                    integer("US"));
    case NC_INT:
      return joined(att_values<std::int32_t>(ncid, varid, name, len),
                    integer(""));
    case NC_UINT:
      return joined(att_values<std::uint32_t>(ncid, varid, name, len),
                    integer("U"));
    case NC_INT64:
      return joined(att_values<long long>(ncid, varid, name, len),
                    integer("LL"));
    case NC_UINT64:
      return joined(att_values<unsigned long long>(ncid, varid, name, len),
                    integer("ULL"));
    case NC_FLOAT:
      return joined(att_values<float>(ncid, varid, name, len),
                    [](float x) { return floating(x, 7, "f"); });
    case NC_DOUBLE:
      return joined(att_values<double>(ncid, varid, name, len),
                    [](double x) { return floating(x, 15, ""); });
    default:
      throw std::runtime_error{"ncdump_header: attribute type"};
  }
}

std::string string_values(int ncid, int varid, const char* name,
                          std::size_t len) {
  std::vector<char*> strings(len);
  check(nc_get_att_string(ncid, varid, name, strings.data()), name);
  std::string out;
  for (std::size_t i = 0; i < len; ++i) {
    out += (i == 0 ? "" : ", ") +
           cdl_quoted(strings[i] != nullptr ? strings[i] : "");
  }
  nc_free_string(len, strings.data());
  return out;
}

std::string attribute(int ncid, int varid, std::string_view owner, int index) {
  std::array<char, NC_MAX_NAME + 1> name{};
  check(nc_inq_attname(ncid, varid, index, name.data()), "nc_inq_attname");
  nc_type type = NC_NAT;
  std::size_t len = 0;
  check(nc_inq_att(ncid, varid, name.data(), &type, &len), name.data());
  std::string values;
  if (type == NC_CHAR) {
    std::string text(len, '\0');
    check(nc_get_att_text(ncid, varid, name.data(), text.data()), name.data());
    values = cdl_quoted(text);
  } else if (type == NC_STRING) {
    values = string_values(ncid, varid, name.data(), len);
  } else {
    values = numeric_values(ncid, varid, name.data(), type, len);
  }
  return std::format("\t\t{}{}:{} = {} ;\n", type == NC_STRING ? "string " : "",
                     owner, name.data(), values);
}

std::string attributes(int ncid, int varid, std::string_view owner) {
  int natts = 0;
  check(nc_inq_varnatts(ncid, varid, &natts), "nc_inq_varnatts");
  std::string out;
  for (int a = 0; a < natts; ++a) {
    out += attribute(ncid, varid, owner, a);
  }
  return out;
}

std::string dim_name(int ncid, int dimid) {
  std::array<char, NC_MAX_NAME + 1> name{};
  check(nc_inq_dimname(ncid, dimid, name.data()), "nc_inq_dimname");
  return name.data();
}

std::string dimensions(int ncid) {
  int ndims = 0;
  check(nc_inq_ndims(ncid, &ndims), "nc_inq_ndims");
  if (ndims == 0) {
    return "";
  }
  int nunlim = 0;
  std::array<int, NC_MAX_DIMS> unlimited{};
  check(nc_inq_unlimdims(ncid, &nunlim, unlimited.data()), "nc_inq_unlimdims");
  std::string out = "dimensions:\n";
  for (int d = 0; d < ndims; ++d) {
    std::size_t len = 0;
    check(nc_inq_dimlen(ncid, d, &len), "nc_inq_dimlen");
    const bool is_unlimited =
        std::find(unlimited.begin(), unlimited.begin() + nunlim, d) !=
        unlimited.begin() + nunlim;
    out += is_unlimited ? std::format("\t{} = UNLIMITED ; // ({} currently)\n",
                                      dim_name(ncid, d), len)
                        : std::format("\t{} = {} ;\n", dim_name(ncid, d), len);
  }
  return out;
}

std::string variable(int ncid, int varid) {
  std::array<char, NC_MAX_NAME + 1> name{};
  nc_type type = NC_NAT;
  int ndims = 0;
  std::array<int, NC_MAX_VAR_DIMS> dims{};
  check(
      nc_inq_var(ncid, varid, name.data(), &type, &ndims, dims.data(), nullptr),
      "nc_inq_var");
  std::string shape;
  for (int d = 0; d < ndims; ++d) {
    shape += (d == 0 ? "(" : ", ") +
             dim_name(ncid, dims.at(static_cast<std::size_t>(d)));
  }
  if (ndims > 0) {
    shape += ")";
  }
  return std::format("\t{} {}{} ;\n", type_name(type), name.data(), shape) +
         attributes(ncid, varid, name.data());
}

}  // namespace

std::string ncdump_header(const std::filesystem::path& path,
                          std::string_view name) {
  const Raw file{path, Raw::Open{}};
  const int ncid = file.id();
  std::string out = std::format("netcdf {} {{\n", name) + dimensions(ncid);
  int nvars = 0;
  check(nc_inq_nvars(ncid, &nvars), "nc_inq_nvars");
  if (nvars > 0) {
    out += "variables:\n";
  }
  for (int v = 0; v < nvars; ++v) {
    out += variable(ncid, v);
  }
  int ngatts = 0;
  check(nc_inq_natts(ncid, &ngatts), "nc_inq_natts");
  if (ngatts > 0) {
    out += "\n// global attributes:\n" + attributes(ncid, NC_GLOBAL, "");
  }
  return out + "}\n";
}

void make_ncdump_probe(const std::filesystem::path& path) {
  Raw file{path};
  const int n = file.dim("n", 3);
  const int u = file.dim("u", NC_UNLIMITED);
  const int ncid = file.id();
  const auto put = [ncid](int varid, const char* name, nc_type type,
                          std::size_t len, const void* values) {
    check(nc_put_att(ncid, varid, name, type, len, values), name);
  };
  const int b = file.var("b", NC_BYTE, {n});
  const std::array<signed char, 3> bytes{-128, 0, 1};
  put(b, "bytes", NC_BYTE, bytes.size(), bytes.data());
  const int s = file.var("s", NC_SHORT, {n});
  const std::array<std::int16_t, 2> shorts{-32768, 7};
  put(s, "shorts", NC_SHORT, shorts.size(), shorts.data());
  const int i = file.var("i", NC_INT, {});
  const std::array<std::int32_t, 2> ints{-2147483647, 42};
  put(i, "ints", NC_INT, ints.size(), ints.data());
  const int l = file.var("l", NC_INT64, {u});
  const std::array<long long, 2> longs{-9007199254740993LL, 5};
  put(l, "longs", NC_INT64, longs.size(), longs.data());
  const int f = file.var("f", NC_FLOAT, {n});
  const std::array<float, 5> floats{9.96921e36F, 0.1F, -2.5F, 1e-10F, 0.0F};
  put(f, "floats", NC_FLOAT, floats.size(), floats.data());
  const int d = file.var("d", NC_DOUBLE, {n, u});
  const double fill = -99999.0;
  check(nc_def_var_fill(ncid, d, 0, &fill), "_FillValue");
  const std::array<double, 8> doubles{
      0.0,  6378137.0, 298.257223563, 9.969209968386869e36,
      1e20, -1.5,      0.1,           std::nan("")};
  put(d, "doubles", NC_DOUBLE, doubles.size(), doubles.data());
  const int c = file.var("c", NC_CHAR, {n});
  file.text(c, "text", "q\"uote \\ it's\ttab\nline \x01 \xC3\xA9");
  const int str = file.var("str", NC_STRING, {n});
  std::array<const char*, 2> strings{"one", "two \"q\""};
  check(nc_put_att_string(ncid, str, "strings", strings.size(), strings.data()),
        "strings");
  const int ub = file.var("ub", NC_UBYTE, {});
  const std::array<unsigned char, 1> ubytes{255};
  put(ub, "ubytes", NC_UBYTE, ubytes.size(), ubytes.data());
  const int us = file.var("us", NC_USHORT, {});
  const std::array<std::uint16_t, 1> ushorts{65535};
  put(us, "ushorts", NC_USHORT, ushorts.size(), ushorts.data());
  const int ui = file.var("ui", NC_UINT, {});
  const std::array<std::uint32_t, 1> uints{4294967295U};
  put(ui, "uints", NC_UINT, uints.size(), uints.data());
  const int ul = file.var("ul", NC_UINT64, {});
  const std::array<unsigned long long, 1> ulongs{18446744073709551615ULL};
  put(ul, "ulongs", NC_UINT64, ulongs.size(), ulongs.data());
  file.text(NC_GLOBAL, "title", "ncdump probe");
  const std::array<double, 1> version{1.0};
  put(NC_GLOBAL, "version", NC_DOUBLE, version.size(), version.data());
  file.enddef();
  const std::array<long long, 2> records{1, 2};
  const std::array<std::size_t, 1> start{0};
  const std::array<std::size_t, 1> count{2};
  check(
      nc_put_vara_longlong(ncid, l, start.data(), count.data(), records.data()),
      "l");
  file.close();
}

std::string without_app_version(std::string_view cdl) {
  static const std::regex version{R"(MetOceanViewer [0-9]+\.[0-9]+\.[0-9]+)"};
  return std::regex_replace(std::string{cdl}, version, "MetOceanViewer X.Y.Z");
}

}  // namespace mov::test::ncgen
