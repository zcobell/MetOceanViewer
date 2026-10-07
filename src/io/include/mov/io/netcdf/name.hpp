// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace mov::io::nc {

/// NC_MAX_NAME of netCDF-C, mirrored so this header does not include
/// <netcdf.h>; src/io/netcdf/file.cpp static_asserts the two are equal.
inline constexpr std::size_t nc_max_name = 256;

enum class NcNameError : std::uint8_t {
  empty,
  too_long,      // more than nc_max_name bytes
  embedded_nul,  // netCDF-C reads names as C strings: a NUL would cut it
};

namespace detail {

/// Why `name` cannot be a netCDF name, if it cannot.
[[nodiscard]] constexpr std::expected<void, NcNameError> check_name(
    std::string_view name) noexcept {
  if (name.empty()) {
    return std::unexpected{NcNameError::empty};
  }
  if (name.size() > nc_max_name) {
    return std::unexpected{NcNameError::too_long};
  }
  if (name.find('\0') != std::string_view::npos) {
    return std::unexpected{NcNameError::embedded_nul};
  }
  return {};
}

/// Not constexpr on purpose: calling it in a consteval constructor makes an
/// invalid name literal a compile error that names this function.
inline void invalid_netcdf_name_literal() noexcept {}

}  // namespace detail

class NcName;

/// A non-owning view of a valid netCDF name, NUL-terminated: the parameter
/// type of every function of the wrapper that takes a name. It is made from a
/// string literal, checked at compile time, or from an NcName that outlives
/// it; never from a temporary NcName.
class NcNameRef {
 public:
  template <std::size_t N>
  // NOLINTNEXTLINE(modernize-avoid-c-arrays): a string literal is the input
  consteval NcNameRef(const char (&literal)[N]) : data_{literal}, size_{N - 1} {
    if (literal[N - 1] != '\0' or
        not detail::check_name(std::string_view{literal, N - 1})) {
      detail::invalid_netcdf_name_literal();
    }
  }
  NcNameRef(const NcName& name) noexcept;
  NcNameRef(const NcName&&) = delete;

  [[nodiscard]] constexpr const char* c_str() const noexcept { return data_; }
  [[nodiscard]] constexpr std::string_view view() const noexcept {
    return {data_, size_};
  }

  friend constexpr bool operator==(NcNameRef a, NcNameRef b) noexcept {
    return a.view() == b.view();
  }

 private:
  const char* data_;
  std::size_t size_;
};

/// An owning, validated netCDF name (C12): non-empty, at most nc_max_name
/// bytes, without an embedded NUL. netCDF-C's own syntax rules (no '/', NFC
/// UTF-8) are left to the library, which reports them when a name is defined.
class NcName {
 public:
  [[nodiscard]] static std::expected<NcName, NcNameError> make(
      std::string_view name) {
    return detail::check_name(name).transform(
        [name] { return NcName{std::string{name}}; });
  }
  /// A copy of a name that is already valid.
  explicit NcName(NcNameRef name) : s_{name.view()} {}

  /// The C string netCDF-C takes; valid while this NcName lives.
  [[nodiscard]] const char* c_str() const& noexcept { return s_.c_str(); }
  const char* c_str() const&& = delete;
  [[nodiscard]] std::string_view view() const& noexcept { return s_; }
  std::string_view view() const&& = delete;

  friend bool operator==(const NcName&, const NcName&) = default;

 private:
  explicit NcName(std::string s) : s_{std::move(s)} {}

  std::string s_;
};

inline NcNameRef::NcNameRef(const NcName& name) noexcept
    : data_{name.c_str()}, size_{name.view().size()} {}

}  // namespace mov::io::nc
