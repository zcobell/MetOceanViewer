// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/// What the app sends to the web services, as values
/// (docs/providers-design.md section 4.1): the origins it talks to and the
/// requests it composes. Nothing here encodes a URL; the Qt edge does that.
namespace mov::io::wire {

/// A scheme, host and port the app talks to; every one is https. The two
/// CO-OPS origins share a host but are distinct origins for the fetch
/// policies (the data API and the metadata API).
enum class Origin : std::uint8_t {
  coops_data,
  coops_metadata,
  usgs,
  ndbc,
  github
};

/// The origin as "https://host": no port (443), no trailing slash.
[[nodiscard]] constexpr std::string_view base(Origin origin) noexcept {
  switch (origin) {
    case Origin::coops_data:
    case Origin::coops_metadata:
      return "https://api.tidesandcurrents.noaa.gov";
    case Origin::usgs:
      return "https://api.waterdata.usgs.gov";
    case Origin::ndbc:
      return "https://www.ndbc.noaa.gov";
    case Origin::github:
      break;
  }
  return "https://api.github.com";
}

/// One query parameter, unencoded: the Qt edge percent-encodes it.
struct QueryParam {
  std::string name;
  std::string value;
  friend bool operator==(const QueryParam&, const QueryParam&) = default;
};

/// A request the app composes: an absolute path on `origin` and its
/// parameters, unencoded and in a fixed order, so a recorded exchange matches
/// byte for byte.
struct Composed {
  Origin origin;
  std::string path;
  std::vector<QueryParam> query;
  friend bool operator==(const Composed&, const Composed&) = default;
};

}  // namespace mov::io::wire
