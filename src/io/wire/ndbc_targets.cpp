// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The NDBC file requests (docs/providers-design.md sections 1.3 and 4.1).

#include <array>
#include <chrono>
#include <cstddef>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "mov/core/overloaded.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/wire/ndbc.hpp"
#include "mov/io/wire/origin.hpp"

namespace mov::io::wire::ndbc {

namespace {

constexpr std::array<std::string_view, 12> month_directories{
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

constexpr std::array<std::string_view, 12> letter_codes{
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c"};

constexpr std::array<std::string_view, 12> decimal_codes{
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"};

// File names spell the year with four digits.
constexpr std::chrono::year first_named_year{0};
constexpr std::chrono::year last_named_year{9999};

using Path = std::expected<std::string, TargetError>;

[[nodiscard]] Path year_text(std::chrono::year y) {
  if (y < first_named_year or y > last_named_year) {
    return std::unexpected{TargetError{TargetErrc::year_out_of_range}};
  }
  return std::format("{:04}", static_cast<int>(y));
}

[[nodiscard]] Path historical_path(const Station& station, Historical h) {
  const std::string id = detail::to_lower_ascii(station.value());
  return year_text(h.year).transform([&](const std::string& yyyy) {
    return std::format("/data/historical/stdmet/{}h{}.txt.gz", id, yyyy);
  });
}

[[nodiscard]] Path monthly_path(const Station& station, Monthly m) {
  const auto directory = month_directory(m.month.month());
  const auto code = month_code(m.month.month(), m.spelling);
  if (not directory or not code) {
    return std::unexpected{TargetError{TargetErrc::month_out_of_range}};
  }
  const std::string id = detail::to_lower_ascii(station.value());
  return year_text(m.month.year()).transform([&](const std::string& yyyy) {
    return std::format("/data/stdmet/{}/{}{}{}.txt.gz", *directory, id, *code,
                       yyyy);
  });
}

[[nodiscard]] Path latest_path(const Station& station, LatestMonth m) {
  const auto directory = month_directory(m.month);
  if (not directory) {
    return std::unexpected{TargetError{TargetErrc::month_out_of_range}};
  }
  return std::format("/data/stdmet/{}/{}.txt", *directory,
                     detail::to_lower_ascii(station.value()));
}

[[nodiscard]] std::string realtime_path(const Station& station) {
  return std::format("/data/realtime2/{}.txt",
                     detail::to_upper_ascii(station.value()));
}

[[nodiscard]] Composed on_ndbc(std::string path) {
  return {.origin = Origin::ndbc, .path = std::move(path), .query = {}};
}

}  // namespace

std::optional<std::string_view> month_directory(std::chrono::month m) noexcept {
  if (not m.ok()) {
    return std::nullopt;
  }
  return month_directories[static_cast<unsigned>(m) - 1];
}

std::optional<std::string_view> month_code(std::chrono::month m,
                                           MonthSpelling spelling) noexcept {
  if (not m.ok()) {
    return std::nullopt;
  }
  const std::size_t i = static_cast<unsigned>(m) - 1;
  return spelling == MonthSpelling::letter ? letter_codes[i] : decimal_codes[i];
}

std::expected<Composed, TargetError> file_target(const Station& station,
                                                 const File& file) {
  return std::visit(
             core::Overloaded{
                 [&](Historical h) { return historical_path(station, h); },
                 [&](Monthly m) { return monthly_path(station, m); },
                 [&](LatestMonth m) { return latest_path(station, m); },
                 [&](Realtime) { return Path{realtime_path(station)}; }},
             file)
      .transform(on_ndbc);
}

Composed active_stations_target() { return on_ndbc("/activestations.xml"); }

}  // namespace mov::io::wire::ndbc
