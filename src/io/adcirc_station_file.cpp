// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/detail/ascii.hpp"
#include "mov/core/detail/utf8.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/station.hpp"
#include "mov/io/adcirc_ascii.hpp"
#include "mov/io/detail/line_cursor.hpp"
#include "mov/io/detail/parse_at.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/error.hpp"
#include "mov/io/projection.hpp"
#include "mov/io/read.hpp"
#include "mov/io/text_file.hpp"
#include "mov/io/warning.hpp"

namespace mov::io {

namespace {

using Line = detail::LineCursor::Line;

// The smallest station line ("1 2\n") and so the most lines a text can hold.
constexpr std::size_t min_line_bytes = 4;

constexpr bool is_separator(char c) noexcept {
  return c == ',' or core::detail::is_space(c);
}

// The next run of non-separators in `rest`, comma and white space both
// separating; `rest` moves past it.
std::optional<std::string_view> next_field(std::string_view& rest) noexcept {
  const auto first = std::ranges::find_if_not(rest, is_separator);
  rest.remove_prefix(static_cast<std::size_t>(first - rest.begin()));
  if (rest.empty()) {
    return std::nullopt;
  }
  const auto last = std::ranges::find_if(rest, is_separator);
  const std::string_view field =
      rest.substr(0, static_cast<std::size_t>(last - rest.begin()));
  rest.remove_prefix(field.size());
  return field;
}

std::optional<Line> next_nonblank(detail::LineCursor& cursor) noexcept {
  while (const auto line = cursor.next()) {
    if (not detail::skip_space(line->text).empty()) {
      return line;
    }
  }
  return std::nullopt;
}

// `text` with every byte that does not start or continue a well-formed UTF-8
// sequence replaced by U+FFFD.
std::string with_valid_utf8(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  while (not text.empty()) {
    const std::size_t length = core::detail::utf8_sequence_length(text);
    if (length == 0) {
      out += "\xEF\xBF\xBD";
      text.remove_prefix(1);
    } else {
      out += text.substr(0, length);
      text.remove_prefix(length);
    }
  }
  return out;
}

std::string crs_subject(core::Epsg crs) {
  return "EPSG:" + std::to_string(crs.code());
}

FormatError unsupported_crs(core::Epsg crs) {
  return FormatError{.code = FormatErrc::unsupported_crs,
                     .subject = crs_subject(crs),
                     .station = std::nullopt,
                     .index = std::nullopt};
}

// One line of the station file: lon, lat, then the name's words.
struct StationLine {
  std::string_view x;
  std::string_view y;
  std::string_view name_words;  // what follows the two coordinates
};

std::expected<StationLine, ParseError> split_station_line(const Line& line) {
  std::string_view rest = line.text;
  const auto x = next_field(rest);
  const auto y = next_field(rest);
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
  while (const auto word = next_field(rest)) {
    if (not name.empty()) {
      name += ' ';
    }
    name += *word;
  }
  return name;
}

// A position that is not a Location blames the latitude when that is what is
// wrong, the first coordinate otherwise.
ParseError position_error(const Line& line, const StationLine& fields,
                          const ToLocationError& why) {
  const auto* location = std::get_if<core::LocationError>(&why);
  const bool latitude = location != nullptr and
                        *location == core::LocationError::latitude_out_of_range;
  return detail::at(line, latitude ? fields.y : fields.x,
                    ParseErrc::out_of_range);
}

class StationFileReader {
 public:
  StationFileReader(std::string_view text, Projector projector)
      : text_{text}, cursor_{text}, projector_{std::move(projector)} {}

  std::expected<Read<std::vector<core::FileStation>>, Error> read() {
    const auto count = read_count();
    if (not count) {
      return std::unexpected{Error{count.error()}};
    }
    stations_.reserve(std::min(*count, text_.size() / min_line_bytes));
    while (stations_.size() < *count) {
      const auto line = next_nonblank(cursor_);
      if (not line) {
        return std::unexpected{Error{too_few()}};
      }
      if (auto added = add_station(*line); not added) {
        return std::unexpected{std::move(added.error())};
      }
    }
    if (const auto extra = next_nonblank(cursor_)) {
      return std::unexpected{
          Error{detail::at(*extra, extra->text, ParseErrc::count_mismatch)}};
    }
    return finish();
  }

 private:
  std::expected<std::size_t, ParseError> read_count() {
    const auto line = next_nonblank(cursor_);
    if (not line) {
      return std::unexpected{
          ParseError::make(ParseErrc::empty_input, {.line = 1}, "")};
    }
    std::string_view rest = line->text;
    // A non-blank line has a first word.
    const std::string_view token =
        detail::next_word(rest).value_or(std::string_view{});
    return detail::int_at<std::size_t>(*line, token);
  }

  [[nodiscard]] ParseError too_few() const {
    return ParseError::make(ParseErrc::count_mismatch,
                            {.line = cursor_.lines_read()}, "too few stations");
  }

  std::expected<void, Error> add_station(const Line& line) {
    const auto fields = split_station_line(line);
    if (not fields) {
      return std::unexpected{Error{fields.error()}};
    }
    const auto x = detail::double_at(line, fields->x);
    const auto y = detail::double_at(line, fields->y);
    if (not x) {
      return std::unexpected{Error{x.error()}};
    }
    if (not y) {
      return std::unexpected{Error{y.error()}};
    }
    const auto where = projector_.to_location(core::Xy{.x = *x, .y = *y});
    if (not where) {
      return std::unexpected{
          Error{position_error(line, *fields, where.error())}};
    }
    return append(*where, core::Xy{.x = *x, .y = *y}, fields->name_words);
  }

  std::expected<void, Error> append(const core::Location& where, core::Xy xy,
                                    std::string_view name_words) {
    std::string name = joined_words(name_words);
    if (not core::detail::is_valid_utf8(name)) {
      name = with_valid_utf8(name);
      ++names_repaired_;
    }
    const std::string id = std::to_string(stations_.size());
    if (name.empty()) {
      name = "Station " + id;
    }
    auto key = core::StationKey::make(id);
    auto text = core::StationText::make(std::move(name));
    if (not key or not text) {
      // Neither can happen: the id is digits, the name is NUL-free UTF-8.
      return std::unexpected{Error{FormatError{.code = FormatErrc::bad_encoding,
                                               .subject = id,
                                               .station = stations_.size(),
                                               .index = std::nullopt}}};
    }
    std::optional<core::NativePoint> native;
    if (projector_.crs() != core::Epsg::wgs84()) {
      if (auto point = core::NativePoint::make(xy, projector_.crs())) {
        native = *point;
      }
    }
    stations_.push_back({.id = *std::move(key),
                         .name = *std::move(text),
                         .location = where,
                         .native = native,
                         .source = core::DataSource::adcirc});
    return {};
  }

  Read<std::vector<core::FileStation>> finish() {
    std::vector<Warning> warnings;
    if (names_repaired_ > 0) {
      warnings.push_back({.code = WarningCode::invalid_utf8_replaced,
                          .subject = {},
                          .count = names_repaired_});
    }
    if (auto approximate = projector_.approximation_warning()) {
      warnings.push_back(std::move(*approximate));
    }
    return {.value = std::move(stations_), .warnings = std::move(warnings)};
  }

  std::string_view text_;
  detail::LineCursor cursor_;
  Projector projector_;
  std::vector<core::FileStation> stations_;
  std::size_t names_repaired_{0};
};

}  // namespace

std::expected<Read<std::vector<core::FileStation>>, Error>
parse_adcirc_station_file(std::string_view text, core::Epsg crs) {
  auto projector = Projector::make(crs);
  if (not projector) {
    return std::unexpected{Error{unsupported_crs(crs)}};
  }
  return StationFileReader{text, *std::move(projector)}.read();
}

std::expected<Read<std::vector<core::FileStation>>, Error>
read_adcirc_station_file(const std::filesystem::path& path, core::Epsg crs,
                         const ReadContext& ctx) {
  return read_text_file(path, ctx.limits)
      .transform_error(lift<Error>)
      .and_then([crs](const std::string& text) {
        return parse_adcirc_station_file(text, crs);
      });
}

}  // namespace mov::io
