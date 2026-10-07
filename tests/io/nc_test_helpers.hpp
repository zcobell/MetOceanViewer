// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Shared pieces of the netCDF wrapper tests.

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/test/scratch_dir.hpp"
#include "nc_fixtures.hpp"

namespace mov::test::nc {

using io::Error;
using io::LibraryStatus;
using io::NcError;
using io::NcOp;
using io::NcStatus;
using io::ReadLimits;
using io::WrapperFault;

/// The NcError inside an io::Error, or nullopt.
[[nodiscard]] inline std::optional<NcError> nc_error_of(const Error& error) {
  if (const auto* nc = std::get_if<NcError>(&error)) {
    return *nc;
  }
  return std::nullopt;
}

/// The status of the NcError inside `error`, or nullopt.
[[nodiscard]] inline std::optional<NcStatus> status_of(const Error& error) {
  return nc_error_of(error).transform(
      [](const NcError& e) { return e.status; });
}
[[nodiscard]] inline std::optional<NcStatus> status_of(const NcError& error) {
  return error.status;
}

/// The error of a failed result; throws (failing the test) on success.
template <class T, class E>
[[nodiscard]] E error_of(const std::expected<T, E>& result) {
  if (result) {
    throw std::logic_error{"expected an error, got a value"};
  }
  return result.error();
}

/// The value of a successful result; throws (failing the test) on error.
template <class T, class E>
[[nodiscard]] T value_of(std::expected<T, E> result) {
  if (not result) {
    throw std::logic_error{"expected a value, got an error"};
  }
  return *std::move(result);
}

/// The value of an optional; throws (failing the test) when it is empty.
template <class T>
[[nodiscard]] T must(std::optional<T> value) {
  if (not value) {
    throw std::logic_error{"expected a value, got nullopt"};
  }
  return *std::move(value);
}

/// A scratch directory with the fixture files written on demand.
class Fixtures {
 public:
  [[nodiscard]] std::filesystem::path typed() {
    return once("typed.nc", [](const auto& p) { ncgen::make_typed(p); });
  }
  [[nodiscard]] std::filesystem::path masking() {
    return once("masking.nc", [](const auto& p) { ncgen::make_masking(p); });
  }
  [[nodiscard]] std::filesystem::path attributes(std::size_t bytes = 120) {
    return once("attributes.nc",
                [bytes](const auto& p) { ncgen::make_attributes(p, bytes); });
  }
  [[nodiscard]] std::filesystem::path hostile(ncgen::Hostile kind) {
    const auto path =
        dir_ / ("hostile-" + std::to_string(static_cast<int>(kind)) + ".nc");
    ncgen::make_hostile(path, kind);
    return path;
  }
  [[nodiscard]] std::filesystem::path path(const std::string& name) const {
    return dir_ / name;
  }
  [[nodiscard]] const std::filesystem::path& dir() const& {
    return dir_.path();
  }

 private:
  template <class Make>
  std::filesystem::path once(const std::string& name, const Make& make) {
    const auto p = dir_ / name;
    if (not std::filesystem::exists(p)) {
      make(p);
    }
    return p;
  }

  ScratchDir dir_;
};

/// Opens `path` with default limits; throws on failure.
[[nodiscard]] inline io::nc::File open(const std::filesystem::path& path,
                                       const ReadLimits& limits = {}) {
  return value_of(io::nc::File::open(path, limits));
}

}  // namespace mov::test::nc
