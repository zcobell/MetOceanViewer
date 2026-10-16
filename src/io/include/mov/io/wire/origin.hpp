// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
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

/// Why a link the server handed out is not followed.
enum class LinkError : std::uint8_t {
  /// Longer than Verbatim::max_bytes.
  too_long,
  /// A byte outside RFC 3986's URI characters (white space, control and
  /// non-ASCII bytes, `\`, `"`, `<`, `>`, `{`, `}`, `|`, `^`, `` ` ``), a
  /// `#` (a fragment), or a `%` not followed by two hexadecimal digits.
  bad_character,
  /// Not `base(origin)` followed by `/`: another scheme, host or port, or
  /// user information.
  other_origin,
  /// A path other than the pinned one (another collection).
  other_path,
};

/// A link the server handed out (a USGS page's `next`), sent as it is: the
/// Qt edge parses it in strict mode and encodes nothing. make() accepts only
/// the scheme, host and port of `base(origin)` (no port, no user
/// information) and exactly the path `pinned_path`, so a page can send the
/// client neither elsewhere nor to another collection
/// (docs/providers-design.md section 4.1, N B1). The query is the server's
/// and is kept byte for byte.
class Verbatim {
 public:
  /// The longest link followed (the USGS links are about 400 bytes).
  static constexpr std::size_t max_bytes = 8192;

  /// `pinned_path` is an absolute path ("/ogcapi/v1/collections/x/items").
  [[nodiscard]] static std::expected<Verbatim, LinkError> make(
      Origin origin, std::string_view pinned_path, std::string_view url);

  [[nodiscard]] Origin origin() const noexcept { return origin_; }
  /// The whole link, as received.
  [[nodiscard]] const std::string& url() const& noexcept { return url_; }
  const std::string& url() && = delete;

  friend bool operator==(const Verbatim&, const Verbatim&) = default;

 private:
  Verbatim(Origin origin, std::string url)
      : origin_{origin}, url_{std::move(url)} {}

  Origin origin_;
  std::string url_;
};

/// What one exchange asks for: a request the app composes, or a link the
/// server handed out.
using Target = std::variant<Composed, Verbatim>;

/// The origin a target goes to.
[[nodiscard]] inline Origin origin_of(const Target& target) noexcept {
  struct Of {
    Origin operator()(const Composed& c) const noexcept { return c.origin; }
    Origin operator()(const Verbatim& v) const noexcept { return v.origin(); }
  };
  return std::visit(Of{}, target);
}

}  // namespace mov::io::wire
