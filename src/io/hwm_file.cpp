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
#include <variant>
#include <vector>

#include "mov/core/detail/ascii.hpp"
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

// The comma-separated fields of `line`, trimmed, as views of it, up to
// out.size(); returns how many there are, counted no further than
// out.size() + 1.
std::size_t split_fields(std::string_view line,
                         std::span<std::string_view> out) noexcept {
  std::size_t count = 0;
  while (count <= out.size()) {
    const std::size_t comma = line.find(',');
    const std::string_view field = core::detail::trim(line.substr(0, comma));
    if (count < out.size()) {
      out[count] = field;
    }
    ++count;
    if (comma == std::string_view::npos) {
      break;
    }
    line.remove_prefix(comma + 1);
  }
  return count;
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

// The first line is a header when it has something in it and nothing that
// looks like a number.
bool is_header(std::string_view line) {
  const auto fields = detail::split_on(line, ',');
  const auto filled = [](std::string_view f) {
    return not core::detail::trim(f).empty();
  };
  return std::ranges::any_of(fields, filled) and
         std::ranges::none_of(fields, [](std::string_view f) {
           return looks_numeric(core::detail::trim(f));
         });
}

ParseErrc code_of(core::ElevationError why) noexcept {
  return why == core::ElevationError::not_finite ? ParseErrc::bad_number
                                                 : ParseErrc::out_of_range;
}

class HwmReader {
 public:
  HwmReader(std::string_view text, core::LengthUnit unit,
            const ReadContext& ctx)
      : cursor_{text}, unit_{unit}, ctx_{ctx} {}

  std::expected<Read<std::vector<Mark>>, Error> read() {
    while (const auto line = cursor_.next()) {
      if (detail::skip_space(line->text).empty()) {
        continue;
      }
      if (auto done = take(*line); not done) {
        return std::unexpected{std::move(done.error())};
      }
    }
    if (marks_.empty()) {
      return std::unexpected{Error{ParseError::make(
          ParseErrc::empty_input,
          {.line = std::max<std::size_t>(cursor_.lines_read(), 1)}, "")}};
    }
    return Read<std::vector<Mark>>{.value = std::move(marks_),
                                   .warnings = std::move(warnings_)};
  }

 private:
  std::expected<void, Error> take(const Line& line) {
    if (not first_seen_) {
      first_seen_ = true;
      if (is_header(line.text)) {
        warnings_.push_back(
            {.code = WarningCode::header_line_skipped,
             .subject = std::string{detail::truncate_utf8(
                 core::detail::trim(line.text), header_subject_bytes)},
             .count = 1});
        return {};
      }
    }
    if (marks_.size() % rows_per_stop_poll == 0 and
        ctx_.stop.stop_requested()) {
      return std::unexpected{Error{Cancelled{}}};
    }
    if (marks_.size() >= ctx_.limits.max_elements) {
      return std::unexpected{
          Error{ParseError::make(ParseErrc::out_of_range, {.line = line.number},
                                 "more marks than ReadLimits::max_elements")}};
    }
    auto mark = parse_row(line);
    if (not mark) {
      return std::unexpected{Error{std::move(mark.error())}};
    }
    marks_.push_back(*std::move(mark));
    return {};
  }

  [[nodiscard]] std::expected<Mark, ParseError> parse_row(
      const Line& line) const {
    std::array<std::string_view, max_fields> fields{};
    const std::size_t count = split_fields(line.text, fields);
    if (count != value_fields and count != max_fields) {
      return std::unexpected{ParseError::make(
          ParseErrc::wrong_field_count, {.line = line.number}, line.text)};
    }
    const auto lon = detail::double_at(line, fields[0]);
    const auto lat = detail::double_at(line, fields[1]);
    if (not lon) {
      return std::unexpected{lon.error()};
    }
    if (not lat) {
      return std::unexpected{lat.error()};
    }
    const auto where = core::Location::make({.lat = *lat, .lon = *lon});
    if (not where) {
      const bool latitude =
          where.error() == core::LocationError::latitude_out_of_range;
      return std::unexpected{detail::at(line, latitude ? fields[1] : fields[0],
                                        ParseErrc::out_of_range)};
    }
    return elevations(line, *where, std::span{fields}.subspan(2, 3));
  }

  // ground, observed, modeled
  [[nodiscard]] std::expected<Mark, ParseError> elevations(
      const Line& line, const core::Location& where,
      std::span<const std::string_view> fields) const {
    const auto ground = checked(line, fields[0]);
    if (not ground) {
      return std::unexpected{ground.error()};
    }
    const auto observed = checked(line, fields[1]);
    if (not observed) {
      return std::unexpected{observed.error()};
    }
    const auto raw = detail::double_at(line, fields[2]);
    if (not raw) {
      return std::unexpected{raw.error()};
    }
    const auto modeled = core::model_value(*raw, unit_);
    if (not modeled) {
      return std::unexpected{
          detail::at(line, fields[2], code_of(modeled.error()))};
    }
    return Mark{.location = where,
                .ground = *ground,
                .observed = *observed,
                .modeled = *modeled};
  }

  [[nodiscard]] std::expected<core::Length, ParseError> checked(
      const Line& line, std::string_view field) const {
    const auto raw = detail::double_at(line, field);
    if (not raw) {
      return std::unexpected{raw.error()};
    }
    const auto length = core::checked_elevation(*raw, unit_);
    if (not length) {
      return std::unexpected{detail::at(line, field, code_of(length.error()))};
    }
    return *length;
  }

  detail::LineCursor cursor_;
  core::LengthUnit unit_;
  const ReadContext& ctx_;
  bool first_seen_{false};
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
