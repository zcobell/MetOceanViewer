// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/ascii.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/station.hpp"
#include "mov/io/adcirc_ascii.hpp"
#include "mov/io/detail/line_cursor.hpp"
#include "mov/io/detail/parse_at.hpp"
#include "mov/io/detail/reporting.hpp"
#include "mov/io/detail/station_names.hpp"
#include "mov/io/detail/table_error.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/error.hpp"
#include "mov/io/projection.hpp"
#include "mov/io/read.hpp"
#include "mov/io/text_file.hpp"
#include "mov/io/warning.hpp"

namespace mov::io {

namespace {

using Line = detail::LineCursor::Line;
using detail::fail;

// The smallest station line ("1 2\n") and so the most lines a text can hold.
constexpr std::size_t min_line_bytes = 4;

constexpr bool is_separator(char c) noexcept {
  return c == ',' or core::ascii::is_space(c);
}

// One line of the station file: lon, lat, then the name's words.
struct StationLine {
  std::string_view x;
  std::string_view y;
  std::string_view name_words;  // what follows the two coordinates
};

std::expected<StationLine, ParseError> split_station_line(const Line& line) {
  std::string_view rest = line.text;
  const auto x = detail::next_word(rest, is_separator);
  const auto y = detail::next_word(rest, is_separator);
  if (not x or not y) {
    return std::unexpected{ParseError::make(ParseErrc::wrong_field_count,
                                            {.line = line.number}, line.text)};
  }
  return StationLine{.x = *x, .y = *y, .name_words = rest};
}

// The name: the words after the coordinates, a NUL ending them, joined by
// single spaces.
std::string joined_words(std::string_view rest) {
  rest = detail::cut_at_nul(rest);
  std::string name;
  while (const auto word = detail::next_word(rest, is_separator)) {
    if (not name.empty()) {
      name += ' ';
    }
    name += *word;
  }
  return name;
}

class StationFileReader {
 public:
  StationFileReader(std::string_view text, Projector projector,
                    const ReadContext& ctx)
      : text_{text},
        cursor_{text},
        projector_{std::move(projector)},
        ctx_{ctx} {}

  std::expected<Read<std::vector<core::FileStation>>, Error> read() && {
    const auto count = read_count();
    if (not count) {
      return fail(count.error());
    }
    detail::reserve_capped(stations_, *count, text_.size() / min_line_bytes);
    while (stations_.size() < *count) {
      if (stations_.size() % stations_per_stop_poll == 0 and
          ctx_.stop.stop_requested()) {
        return std::unexpected{Error{Cancelled{}}};
      }
      const auto line = cursor_.next_nonblank();
      if (not line) {
        return fail(too_few());
      }
      if (auto added = add_station(*line); not added) {
        return std::unexpected{std::move(added.error())};
      }
    }
    if (const auto extra = cursor_.next_nonblank()) {
      return fail(detail::at(*extra, extra->text, ParseErrc::count_mismatch));
    }
    return std::move(*this).finish();
  }

 private:
  static constexpr std::size_t stations_per_stop_poll = 1024;

  // The first field of the first non-blank line.
  std::expected<std::size_t, ParseError> read_count() {
    const auto line = cursor_.next_nonblank();
    if (not line) {
      return std::unexpected{
          ParseError::make(ParseErrc::empty_input, {.line = 1}, "")};
    }
    std::string_view rest = line->text;
    // A non-blank line has a first field (a line of commas has none).
    const std::string_view token =
        detail::next_word(rest, is_separator).value_or(rest);
    const auto count = detail::int_at<std::size_t>(*line, token);
    if (count and *count > ctx_.limits.max_elements) {
      return std::unexpected{detail::at(*line, token, ParseErrc::too_large)};
    }
    return count;
  }

  [[nodiscard]] ParseError too_few() const {
    return ParseError::make(ParseErrc::count_mismatch,
                            {.line = cursor_.lines_read()}, "too few stations");
  }

  std::expected<void, Error> add_station(const Line& line) {
    const auto fields = split_station_line(line);
    if (not fields) {
      return fail(fields.error());
    }
    const auto x = detail::double_at(line, fields->x);
    const auto y = detail::double_at(line, fields->y);
    if (not x) {
      return fail(x.error());
    }
    if (not y) {
      return fail(y.error());
    }
    const auto where = projector_.to_location(core::Xy{.x = *x, .y = *y});
    if (not where) {
      return fail_position(line, *fields, where.error());
    }
    return append(*where, core::Xy{.x = *x, .y = *y}, fields->name_words);
  }

  // A transformation PROJ could not make is a FormatError of that station; a
  // position that is not a Location is a ParseError at its field.
  [[nodiscard]] std::expected<void, Error> fail_position(
      const Line& line, const StationLine& fields,
      const ToLocationError& why) const {
    if (const auto* location = std::get_if<core::LocationError>(&why)) {
      return fail(detail::position_at(line, fields.x, fields.y, *location));
    }
    return fail(detail::to_format_error(std::get<ProjectionError>(why),
                                        stations_.size()));
  }

  std::expected<void, Error> append(const core::Location& where, core::Xy xy,
                                    std::string_view name_words) {
    const std::size_t index = stations_.size();
    // The name was cut at a NUL by joined_words, so only bad UTF-8 is left.
    detail::CleanedText name =
        detail::replace_invalid_utf8(joined_words(name_words));
    if (name.replaced) {
      ++names_repaired_;
    }
    const std::string id = std::to_string(index);
    auto key = core::StationKey::make(id);
    if (not key) {
      return fail(detail::to_format_error(key.error(), index));
    }
    if (name.text.empty()) {
      auto fallback = core::StationText::make("Station " + id);
      if (not fallback) {
        return fail(detail::to_format_error(fallback.error(), index));
      }
      name.text = *std::move(fallback);
    }
    std::optional<core::NativePoint> native;
    if (projector_.crs() != core::Epsg::wgs84()) {
      // Finite by construction: double_at accepts no NaN or infinity.
      if (auto point = core::NativePoint::make(xy, projector_.crs())) {
        native = *point;
      }
    }
    stations_.push_back({.id = *std::move(key),
                         .name = std::move(name.text),
                         .location = where,
                         .native = native,
                         .source = core::DataSource::adcirc});
    return {};
  }

  Read<std::vector<core::FileStation>> finish() && {
    std::vector<Warning> warnings;
    append_if_counted(warnings, {.code = WarningCode::invalid_utf8_replaced,
                                 .subject = {},
                                 .count = names_repaired_});
    if (auto approximate = projector_.approximation_warning()) {
      warnings.push_back(std::move(*approximate));
    }
    return {.value = std::move(stations_), .warnings = std::move(warnings)};
  }

  std::string_view text_;
  detail::LineCursor cursor_;
  Projector projector_;
  const ReadContext& ctx_;
  std::vector<core::FileStation> stations_;
  std::size_t names_repaired_{0};
};

}  // namespace

std::expected<Read<std::vector<core::FileStation>>, Error>
parse_adcirc_station_file(std::string_view text, core::Epsg crs,
                          const ReadContext& ctx) {
  auto projector = Projector::make(crs);
  if (not projector) {
    return fail(detail::to_format_error(projector.error(), std::nullopt));
  }
  return StationFileReader{text, *std::move(projector), ctx}.read();
}

std::expected<Read<std::vector<core::FileStation>>, Error>
read_adcirc_station_file(const std::filesystem::path& path, core::Epsg crs,
                         const ReadContext& ctx) {
  return read_text_file(path, ctx.limits)
      .transform_error(lift<Error>)
      .and_then([crs, &ctx](const std::string& text) {
        return parse_adcirc_station_file(text, crs, ctx);
      });
}

}  // namespace mov::io
