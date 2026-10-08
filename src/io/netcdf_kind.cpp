// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "netcdf_kind.hpp"

#include <array>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "model_netcdf.hpp"
#include "mov/core/detail/ascii.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/station_netcdf.hpp"
#include "station_netcdf_format.hpp"
#include "station_netcdf_reader.hpp"

namespace mov::io::detail {

namespace {

namespace sn = station_nc;

std::expected<bool, Error> has_var(const nc::File& file, nc::NcNameRef name) {
  auto found = file.find_var(name);
  if (not found) {
    return fail(std::move(found).error());
  }
  return found->has_value();
}

std::expected<bool, Error> has_dim(const nc::File& file, nc::NcNameRef name) {
  auto found = file.find_dim(name);
  if (not found) {
    return fail(std::move(found).error());
  }
  return found->has_value();
}

/// Whether global attribute `name` is the text `value` (trimmed).
std::expected<bool, Error> global_is(const nc::File& file, nc::NcNameRef name,
                                     std::string_view value,
                                     bool ignore_case = false) {
  return sn::text_of(file, nc::global, name)
      .transform([&](const std::optional<std::string>& text) {
        if (not text) {
          return false;
        }
        const std::string_view t = core::detail::trim(*text);
        return ignore_case ? core::detail::equal_ignore_case(t, value)
                           : t == value;
      });
}

std::expected<bool, Error> is_dflow(const nc::File& file) {
  auto x = has_var(file, "station_x_coordinate");
  if (not x or not *x) {
    return x;
  }
  return has_var(file, "station_y_coordinate");
}

std::expected<bool, Error> is_foreign_cf(const nc::File& file) {
  auto feature = global_is(file, "featureType", sn::feature_type, true);
  if (not feature or not *feature) {
    return feature;
  }
  return sn::text_of(file, nc::global, "Conventions")
      .transform([](const std::optional<std::string>& text) {
        const auto cf = text ? parse_cf_conventions(*text) : std::nullopt;
        return cf.has_value() and cf->major == 1 and cf->minor >= 6;
      });
}

std::expected<bool, Error> is_legacy(const nc::File& file) {
  auto four = has_var(file, "time_station_0001");
  if (not four or *four) {
    return four;
  }
  auto six = has_var(file, "time_station_000001");
  if (not six or not *six) {
    return six;
  }
  // CRMS (dialect C) has the six-digit names too, but neither of these.
  auto stations = has_dim(file, "numStations");
  if (not stations or not *stations) {
    return stations;
  }
  return has_var(file, "stationXCoordinate");
}

}  // namespace

std::expected<NetcdfKind, Error> classify_netcdf(const nc::File& file) {
  // A file that names its own format is that format or none of ours: it is
  // not guessed to be a foreign CF file (a future major version of the format
  // may keep the CF attributes).
  auto format = sn::text_of(file, nc::global, "metoceanviewer_format");
  if (not format) {
    return std::unexpected{std::move(format).error()};
  }
  if (*format) {
    return core::detail::trim(**format) == station_nc_format
               ? NetcdfKind::station_v5
               : NetcdfKind::other;
  }
  struct Test {
    NetcdfKind kind;
    std::expected<bool, Error> (*matches)(const nc::File&);
  };
  static constexpr std::array<Test, 4> tests{{
      {.kind = NetcdfKind::adcirc,
       .matches =
           [](const nc::File& f) { return global_is(f, "model", "ADCIRC"); }},
      {.kind = NetcdfKind::dflow, .matches = is_dflow},
      {.kind = NetcdfKind::foreign_cf, .matches = is_foreign_cf},
      {.kind = NetcdfKind::legacy_station, .matches = is_legacy},
  }};
  for (const Test& test : tests) {
    auto matches = test.matches(file);
    if (not matches) {
      return std::unexpected{std::move(matches).error()};
    }
    if (*matches) {
      return test.kind;
    }
  }
  return NetcdfKind::other;
}

}  // namespace mov::io::detail
