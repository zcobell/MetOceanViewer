// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "netcdf_kind.hpp"

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "model_netcdf.hpp"
#include "mov/core/detail/ascii.hpp"
#include "mov/io/detail/text.hpp"
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

/// What a foreign CF `timeSeries` check found: the CF version of the file, or
/// the attribute that stands in the way.
using ForeignCheck = std::variant<CfVersion, kind::Unrecognized>;

std::expected<ForeignCheck, Error> check_foreign_cf(const nc::File& file) {
  auto feature = global_is(file, "featureType", sn::feature_type, true);
  if (not feature) {
    return std::unexpected{std::move(feature).error()};
  }
  if (not *feature) {
    return ForeignCheck{kind::Unrecognized{.subject = ":featureType"}};
  }
  return sn::text_of(file, nc::global, "Conventions")
      .transform([](const std::optional<std::string>& text) {
        const auto cf = text ? parse_cf_conventions(*text) : std::nullopt;
        return cf and cf->major == 1 and cf->minor >= 6
                   ? ForeignCheck{*cf}
                   : ForeignCheck{
                         kind::Unrecognized{.subject = ":Conventions"}};
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

/// The kinds that are decided by a yes or no, in order.
std::expected<std::optional<NetcdfKind>, Error> model_kind(
    const nc::File& file) {
  auto adcirc = global_is(file, "model", "ADCIRC");
  if (not adcirc) {
    return std::unexpected{std::move(adcirc).error()};
  }
  if (*adcirc) {
    return std::optional<NetcdfKind>{kind::Adcirc{}};
  }
  auto dflow = is_dflow(file);
  if (not dflow) {
    return std::unexpected{std::move(dflow).error()};
  }
  return *dflow ? std::optional<NetcdfKind>{kind::Dflow{}} : std::nullopt;
}

}  // namespace

std::expected<NetcdfKind, Error> classify_netcdf(const nc::File& file) {
  // A file that names its own format is that format or none of ours.
  auto format = sn::text_of(file, nc::global, "metoceanviewer_format");
  if (not format) {
    return std::unexpected{std::move(format).error()};
  }
  if (*format) {
    const std::string_view name = core::detail::trim(**format);
    if (name == station_nc_format) {
      return NetcdfKind{kind::StationV5{}};
    }
    return NetcdfKind{kind::OtherFormat{.name = sn::subject_of(name)}};
  }
  auto model = model_kind(file);
  if (not model) {
    return std::unexpected{std::move(model).error()};
  }
  if (*model) {
    return **std::move(model);
  }
  auto foreign = check_foreign_cf(file);
  if (not foreign) {
    return std::unexpected{std::move(foreign).error()};
  }
  if (const auto* version = std::get_if<CfVersion>(&*foreign)) {
    return NetcdfKind{kind::ForeignCf{.version = *version}};
  }
  auto legacy = is_legacy(file);
  if (not legacy) {
    return std::unexpected{std::move(legacy).error()};
  }
  if (*legacy) {
    return NetcdfKind{kind::LegacyStation{}};
  }
  return NetcdfKind{std::get<kind::Unrecognized>(std::move(*foreign))};
}

}  // namespace mov::io::detail
