// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/hwm_file.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/core/ascii.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/hwm.hpp"
#include "mov/core/units.hpp"
#include "mov/io/detail/line_cursor.hpp"
#include "mov/io/detail/model_number.hpp"
#include "mov/io/detail/parse_at.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/text_file.hpp"
#include "mov/io/warning.hpp"

namespace mov::io {

namespace {

using Line = detail::LineCursor::Line;
using Mark = core::HighWaterMark;

constexpr std::size_t rows_per_stop_poll = 1024;
constexpr std::size_t header_subject_bytes = 60;

// A row has this many fields; the last, the difference, may be left out.
constexpr std::size_t value_fields = 5;
constexpr std::size_t max_fields = 6;

template <class E>
auto fail(E&& e) {
  return std::unexpected{lift<Error>(std::forward<E>(e))};
}

// The text of the five columns of a row; the views are into its line.
struct RowText {
  std::string_view lon;
  std::string_view lat;
  std::string_view ground;
  std::string_view observed;
  std::string_view modeled;
};

// The comma-separated fields of `line`, trimmed, as views of it, up to
// out.size(); returns how many there are, counted no further than
// out.size() + 1.
std::size_t split_fields(std::string_view line,
                         std::span<std::string_view> out) noexcept {
  const std::size_t count = detail::split_on_into(line, ',', out);
  for (std::string_view& field : out.first(std::min(count, out.size()))) {
    field = core::ascii::trim(field);
  }
  return count;
}

bool has_row_shape(std::size_t fields) noexcept {
  return fields == value_fields or fields == max_fields;
}

// A field that is, or means to be, a number: one parse_double reads or
// refuses only for its size, or a NaN or infinity token. Such a field makes
// its line data, so a bad number is reported instead of skipped as a header.
bool looks_numeric(std::string_view field) {
  const auto number = detail::parse_double(field);
  return number.has_value() or
         number.error() == detail::NumberError::out_of_range or
         detail::is_nonfinite_token(field);
}

// A header names the columns: it has the shape of a row (five or six fields),
// something in it, and nothing that looks like a number.
bool is_header(std::string_view line) {
  std::array<std::string_view, max_fields> fields{};
  const std::size_t count = split_fields(line, fields);
  if (not has_row_shape(count)) {
    return false;
  }
  const auto present = std::span{fields}.first(count);
  return std::ranges::any_of(
             present, [](std::string_view f) { return not f.empty(); }) and
         std::ranges::none_of(present, looks_numeric);
}

std::expected<RowText, ParseError> split_row(const Line& line) {
  std::array<std::string_view, max_fields> fields{};
  if (not has_row_shape(split_fields(line.text, fields))) {
    return std::unexpected{ParseError::make(ParseErrc::wrong_field_count,
                                            {.line = line.number}, line.text)};
  }
  return RowText{.lon = fields[0],
                 .lat = fields[1],
                 .ground = fields[2],
                 .observed = fields[3],
                 .modeled = fields[4]};
}

ParseErrc code_of(core::ElevationError why) noexcept {
  return why == core::ElevationError::not_finite ? ParseErrc::bad_number
                                                 : ParseErrc::out_of_range;
}

// The mark a row means, or what is wrong with it. Columns are checked left to
// right, each for syntax and then for its range.
class RowParser {
 public:
  RowParser(const Line& line, core::LengthUnit unit)
      : line_{line}, unit_{unit} {}

  [[nodiscard]] std::expected<Mark, ParseError> parse(
      const RowText& row) const {
    const auto where = location(row);
    if (not where) {
      return std::unexpected{where.error()};
    }
    const auto ground = checked(row.ground);
    if (not ground) {
      return std::unexpected{ground.error()};
    }
    const auto observed = checked(row.observed);
    if (not observed) {
      return std::unexpected{observed.error()};
    }
    const auto modeled = model(row.modeled);
    if (not modeled) {
      return std::unexpected{modeled.error()};
    }
    return Mark{.location = *where,
                .ground = *ground,
                .observed = *observed,
                .modeled = *modeled};
  }

 private:
  [[nodiscard]] std::expected<core::Location, ParseError> location(
      const RowText& row) const {
    const auto lon = detail::double_at(line_, row.lon);
    const auto lat = detail::double_at(line_, row.lat);
    if (not lon) {
      return std::unexpected{lon.error()};
    }
    if (not lat) {
      return std::unexpected{lat.error()};
    }
    const auto where = core::Location::make({.lat = *lat, .lon = *lon});
    if (not where) {
      return std::unexpected{
          detail::position_at(line_, row.lon, row.lat, where.error())};
    }
    return *where;
  }

  [[nodiscard]] std::expected<core::Length, ParseError> checked(
      std::string_view field) const {
    const auto raw = detail::double_at(line_, field);
    if (not raw) {
      return std::unexpected{raw.error()};
    }
    const auto length = core::checked_elevation(*raw, unit_);
    if (not length) {
      return std::unexpected{detail::at(line_, field, code_of(length.error()))};
    }
    return *length;
  }

  [[nodiscard]] std::expected<core::WetDry, ParseError> model(
      std::string_view field) const {
    const auto raw = detail::double_at(line_, field);
    if (not raw) {
      return std::unexpected{raw.error()};
    }
    const auto value = core::model_value(*raw, unit_);
    if (not value) {
      return std::unexpected{detail::at(line_, field, code_of(value.error()))};
    }
    return *value;
  }

  const Line& line_;
  core::LengthUnit unit_;
};

class HwmReader {
 public:
  HwmReader(std::string_view text, core::LengthUnit unit,
            const ReadContext& ctx)
      : cursor_{text}, unit_{unit}, ctx_{ctx} {}

  std::expected<Read<std::vector<Mark>>, Error> read() && {
    auto line = cursor_.next_nonblank();
    if (line and is_header(line->text)) {
      warnings_.push_back(
          {.code = WarningCode::header_line_skipped,
           .subject = std::string{detail::truncate_utf8(
               core::ascii::trim(line->text), header_subject_bytes)},
           .count = 1});
      line = cursor_.next_nonblank();
    }
    for (; line; line = cursor_.next_nonblank()) {
      if (auto added = add(*line); not added) {
        return std::unexpected{std::move(added.error())};
      }
    }
    if (marks_.empty()) {
      return fail(ParseError::make(
          ParseErrc::empty_input,
          {.line = std::max<std::size_t>(cursor_.lines_read(), 1)}, ""));
    }
    return Read<std::vector<Mark>>{.value = std::move(marks_),
                                   .warnings = std::move(warnings_)};
  }

 private:
  std::expected<void, Error> add(const Line& line) {
    if (marks_.size() % rows_per_stop_poll == 0 and
        ctx_.stop.stop_requested()) {
      return std::unexpected{Error{Cancelled{}}};
    }
    // A mark is value_fields numbers; the limit counts them.
    if (marks_.size() >= ctx_.limits.max_elements / value_fields) {
      return fail(
          ParseError::make(ParseErrc::too_large, {.line = line.number},
                           "more marks than ReadLimits::max_elements allows"));
    }
    const auto row = split_row(line);
    if (not row) {
      return fail(row.error());
    }
    auto mark = RowParser{line, unit_}.parse(*row);
    if (not mark) {
      return fail(mark.error());
    }
    marks_.push_back(*std::move(mark));
    return {};
  }

  detail::LineCursor cursor_;
  core::LengthUnit unit_;
  const ReadContext& ctx_;
  std::vector<Mark> marks_;
  std::vector<Warning> warnings_;
};

}  // namespace

std::expected<Read<std::vector<core::HighWaterMark>>, Error> parse_hwm_csv(
    std::string_view text, core::LengthUnit unit, const ReadContext& ctx) {
  return HwmReader{text, unit, ctx}.read();
}

std::expected<Read<std::vector<core::HighWaterMark>>, Error> read_hwm_csv(
    const std::filesystem::path& path, core::LengthUnit unit,
    const ReadContext& ctx) {
  return read_text_file(path, ctx.limits)
      .transform_error(lift<Error>)
      .and_then([unit, &ctx](const std::string& text) {
        return parse_hwm_csv(text, unit, ctx);
      });
}

}  // namespace mov::io
