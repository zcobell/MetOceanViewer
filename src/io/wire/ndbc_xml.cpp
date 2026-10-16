// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The activestations.xml subset scanner (docs/providers-design.md section
// 4.5): a hand-written recognizer of the few XML constructs the list uses.

#include "mov/io/detail/ndbc_xml.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/core/ascii.hpp"
#include "mov/core/utf8.hpp"
#include "mov/io/detail/text_position.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"

namespace mov::io::detail::ndbc_xml {

namespace {

constexpr std::string_view utf8_bom{"\xEF\xBB\xBF"};
constexpr std::size_t stop_poll_stations = 4096;

// The largest XML character, and how many digits a reference to it may have.
constexpr std::uint32_t max_code_point = 0x10FFFF;
constexpr std::size_t max_hex_digits = 6;
constexpr std::size_t max_decimal_digits = 7;
constexpr std::uint32_t hex_base = 16;
constexpr std::uint32_t decimal_base = 10;

struct Entity {
  std::string_view name;
  char replacement;
};
constexpr std::array<Entity, 5> predefined{
    {{.name = "lt", .replacement = '<'},
     {.name = "gt", .replacement = '>'},
     {.name = "amp", .replacement = '&'},
     {.name = "quot", .replacement = '"'},
     {.name = "apos", .replacement = '\''}}};

// XML 1.0 Char: #x9 | #xA | #xD | [#x20-#xD7FF] | [#xE000-#xFFFD] |
// [#x10000-#x10FFFF].
[[nodiscard]] constexpr bool is_xml_char(std::uint32_t c) noexcept {
  constexpr std::uint32_t surrogates_begin = 0xD800;
  constexpr std::uint32_t private_use_begin = 0xE000;
  constexpr std::uint32_t non_characters_begin = 0xFFFE;
  constexpr std::uint32_t supplementary_begin = 0x10000;
  constexpr std::uint32_t tab = 0x9;
  constexpr std::uint32_t lf = 0xA;
  constexpr std::uint32_t cr = 0xD;
  constexpr std::uint32_t space = 0x20;
  return c == tab or c == lf or c == cr or
         (c >= space and c < surrogates_begin) or
         (c >= private_use_begin and c < non_characters_begin) or
         (c >= supplementary_begin and c <= max_code_point);
}

[[nodiscard]] constexpr bool is_xml_space(char c) noexcept {
  return c == ' ' or c == '\t' or c == '\r' or c == '\n';
}

[[nodiscard]] constexpr bool is_name_start(char c) noexcept {
  return core::ascii::is_alpha(c) or c == '_';
}

[[nodiscard]] constexpr bool is_name_char(char c) noexcept {
  return core::ascii::is_alnum(c) or c == '_' or c == '.' or c == '-';
}

// `c` as UTF-8 (c is an XML character, so at most U+10FFFF).
void append_utf8(std::string& out, std::uint32_t c) {
  constexpr std::uint32_t one_byte = 0x80;
  constexpr std::uint32_t two_bytes = 0x800;
  constexpr std::uint32_t three_bytes = 0x10000;
  constexpr std::uint32_t six_bits = 0x3F;
  constexpr std::uint32_t continuation = 0x80;
  const auto byte = [&out](std::uint32_t b) {
    out.push_back(static_cast<char>(static_cast<unsigned char>(b)));
  };
  if (c < one_byte) {
    byte(c);
  } else if (c < two_bytes) {
    byte(0xC0U | (c >> 6U));
    byte(continuation | (c & six_bits));
  } else if (c < three_bytes) {
    byte(0xE0U | (c >> 12U));
    byte(continuation | ((c >> 6U) & six_bits));
    byte(continuation | (c & six_bits));
  } else {
    byte(0xF0U | (c >> 18U));
    byte(continuation | ((c >> 12U) & six_bits));
    byte(continuation | ((c >> 6U) & six_bits));
    byte(continuation | (c & six_bits));
  }
}

[[nodiscard]] std::optional<std::uint32_t> digit_value(char c) noexcept {
  if (core::ascii::is_digit(c)) {
    return static_cast<std::uint32_t>(c - '0');
  }
  const char lower = core::ascii::to_lower(c);
  if (lower >= 'a' and lower <= 'f') {
    return static_cast<std::uint32_t>(lower - 'a') + decimal_base;
  }
  return std::nullopt;
}

// The offset of the first byte that is not UTF-8 of an XML character, or
// nullopt when every byte is.
[[nodiscard]] std::optional<std::size_t> first_bad_character(
    std::string_view text) noexcept {
  constexpr std::string_view non_characters_prefix{"\xEF\xBF"};
  constexpr unsigned char fffe_last = 0xBE;
  for (std::size_t at = 0; at < text.size();) {
    const std::string_view rest = text.substr(at);
    const std::size_t n = core::utf8_sequence_length(rest);
    if (n == 0) {
      return at;
    }
    const auto lead = static_cast<unsigned char>(rest[0]);
    if (n == 1 and not is_xml_char(lead)) {
      return at;
    }
    if (n == 3 and rest.starts_with(non_characters_prefix) and
        static_cast<unsigned char>(rest[2]) >= fffe_last) {
      return at;  // U+FFFE, U+FFFF
    }
    at += n;
  }
  return std::nullopt;
}

class Scanner {
 public:
  Scanner(std::string_view text, const ReadContext& ctx)
      : text_{text}, ctx_{ctx} {}

  std::expected<Document, Error> scan() && {
    if (text_.starts_with(utf8_bom)) {
      pos_ = utf8_bom.size();
    }
    if (std::ranges::all_of(text_.substr(pos_), is_xml_space)) {
      return fail(ParseErrc::empty_input);
    }
    if (const auto bad = first_bad_character(text_)) {
      pos_ = *bad;
      return fail();
    }
    if (text_.substr(pos_).starts_with("<?xml")) {
      if (auto d = declaration(); not d) {
        return std::unexpected{std::move(d).error()};
      }
    }
    if (auto r = misc().and_then([this] { return root(); }).and_then([this] {
          return misc();
        });
        not r) {
      return std::unexpected{std::move(r).error()};
    }
    if (pos_ != text_.size()) {
      return fail();
    }
    return std::move(doc_);
  }

 private:
  using Step = std::expected<void, Error>;

  [[nodiscard]] std::unexpected<Error> fail(
      ParseErrc code = ParseErrc::corrupt_record) const {
    return std::unexpected{Error{detail::error_at_offset(code, text_, pos_)}};
  }

  [[nodiscard]] bool at_end() const noexcept { return pos_ >= text_.size(); }
  [[nodiscard]] std::string_view rest() const noexcept {
    return text_.substr(std::min(pos_, text_.size()));
  }

  bool consume(std::string_view literal) noexcept {
    if (not rest().starts_with(literal)) {
      return false;
    }
    pos_ += literal.size();
    return true;
  }

  // Skips white space; whether there was any.
  bool skip_space() noexcept {
    const std::size_t before = pos_;
    while (not at_end() and is_xml_space(text_[pos_])) {
      ++pos_;
    }
    return pos_ != before;
  }

  // The 1-based line of pos_, counted forward from the last one asked for.
  [[nodiscard]] std::size_t line() {
    line_ += static_cast<std::size_t>(
        std::ranges::count(text_.substr(counted_, pos_ - counted_), '\n'));
    counted_ = pos_;
    return line_;
  }

  std::expected<std::string_view, Error> name() {
    const std::size_t start = pos_;
    if (at_end() or not is_name_start(text_[pos_])) {
      return fail();
    }
    while (not at_end() and is_name_char(text_[pos_])) {
      ++pos_;
    }
    return text_.substr(start, pos_ - start);
  }

  // A pseudo-attribute of the XML declaration: S name Eq quoted, its value
  // plain ASCII letters, digits, '.', '-' and '_'.
  std::expected<Attribute, Error> pseudo_attribute() {
    auto n = name();
    if (not n) {
      return std::unexpected{std::move(n).error()};
    }
    Attribute a{.name = std::string{*n}, .value = {}};
    skip_space();
    if (not consume("=")) {
      return fail();
    }
    skip_space();
    const char quote = at_end() ? '\0' : text_[pos_];
    if (quote != '"' and quote != '\'') {
      return fail();
    }
    ++pos_;
    while (not at_end() and is_name_char(text_[pos_])) {
      a.value.push_back(text_[pos_++]);
    }
    if (not consume(std::string_view{&quote, 1})) {
      return fail();
    }
    return a;
  }

  [[nodiscard]] static bool declaration_ok(const std::vector<Attribute>& a) {
    std::size_t i = 0;
    if (a.empty() or a[0].name != "version" or a[0].value != "1.0") {
      return false;
    }
    ++i;
    if (i < a.size() and a[i].name == "encoding") {
      if (not core::ascii::equal_ignore_case(a[i].value, "utf-8")) {
        return false;
      }
      ++i;
    }
    if (i < a.size() and a[i].name == "standalone") {
      if (a[i].value != "yes" and a[i].value != "no") {
        return false;
      }
      ++i;
    }
    return i == a.size();
  }

  Step declaration() {
    const std::size_t start = pos_;
    pos_ += std::string_view{"<?xml"}.size();
    std::vector<Attribute> attributes;
    while (true) {
      const bool spaced = skip_space();
      if (consume("?>")) {
        break;
      }
      if (not spaced) {
        return fail();
      }
      auto a = pseudo_attribute();
      if (not a) {
        return std::unexpected{std::move(a).error()};
      }
      attributes.push_back(*std::move(a));
    }
    if (not declaration_ok(attributes)) {
      pos_ = start;
      return fail();
    }
    return {};
  }

  Step comment() {
    const std::size_t end = text_.find("--", pos_);
    if (end == std::string_view::npos) {
      pos_ = text_.size();
      return fail();
    }
    pos_ = end;
    if (not consume("-->")) {
      return fail();
    }
    return {};
  }

  // White space and comments.
  Step misc() {
    while (true) {
      skip_space();
      if (not consume("<!--")) {
        return {};
      }
      if (auto c = comment(); not c) {
        return c;
      }
    }
  }

  // A reference after its '&', appended to `out`.
  Step reference(std::string& out) {
    if (consume("#x")) {
      return character_reference(out, hex_base, max_hex_digits);
    }
    if (consume("#")) {
      return character_reference(out, decimal_base, max_decimal_digits);
    }
    for (const Entity& e : predefined) {
      if (rest().starts_with(e.name) and
          rest().substr(e.name.size()).starts_with(';')) {
        pos_ += e.name.size() + 1;
        out.push_back(e.replacement);
        return {};
      }
    }
    return fail();
  }

  Step character_reference(std::string& out, std::uint32_t base,
                           std::size_t max_digits) {
    std::uint32_t c = 0;
    std::size_t digits = 0;
    while (not at_end() and text_[pos_] != ';') {
      const auto d = digit_value(text_[pos_]);
      if (not d or *d >= base or ++digits > max_digits) {
        return fail();
      }
      c = c * base + *d;
      ++pos_;
    }
    if (digits == 0 or not consume(";") or not is_xml_char(c)) {
      return fail();
    }
    append_utf8(out, c);
    return {};
  }

  Step quoted(std::string& out) {
    const char quote = at_end() ? '\0' : text_[pos_];
    if (quote != '"' and quote != '\'') {
      return fail();
    }
    ++pos_;
    while (not at_end() and text_[pos_] != quote) {
      const std::size_t amp = pos_;
      const char c = text_[pos_];
      if (c == '<' or static_cast<unsigned char>(c) < ' ') {
        return fail();
      }
      ++pos_;
      if (c == '&') {
        if (auto r = reference(out); not r) {
          pos_ = amp;  // a bad reference is reported at its '&'
          return fail();
        }
      } else {
        out.push_back(c);
      }
    }
    if (at_end()) {
      return fail();
    }
    ++pos_;
    return {};
  }

  Step attribute(std::vector<Attribute>& into) {
    auto n = name();
    if (not n) {
      return std::unexpected{std::move(n).error()};
    }
    const std::size_t at = pos_ - n->size();
    const bool reserved =
        core::ascii::equal_ignore_case(n->substr(0, 3), "xml");
    const bool repeated = std::ranges::any_of(
        into, [&](const Attribute& a) { return a.name == *n; });
    if (reserved or repeated) {
      pos_ = at;
      return fail();
    }
    if (into.size() == max_attributes) {
      pos_ = at;
      return fail(ParseErrc::too_large);
    }
    Attribute a{.name = std::string{*n}, .value = {}};
    skip_space();
    if (not consume("=")) {
      return fail();
    }
    skip_space();
    if (auto v = quoted(a.value); not v) {
      return v;
    }
    into.push_back(std::move(a));
    return {};
  }

  enum class Close : std::uint8_t { empty, open };

  // An element's attributes, up to and including its '/>' or '>'.
  std::expected<Close, Error> attributes(std::vector<Attribute>& into) {
    while (true) {
      const bool spaced = skip_space();
      if (consume("/>")) {
        return Close::empty;
      }
      if (consume(">")) {
        return Close::open;
      }
      if (not spaced) {
        return fail();
      }
      if (auto a = attribute(into); not a) {
        return std::unexpected{std::move(a).error()};
      }
    }
  }

  // '<' and `expected` as a whole name.
  Step start_tag(std::string_view expected) {
    const std::size_t at = pos_;
    if (not consume("<")) {
      return fail();
    }
    auto n = name();
    if (not n or *n != expected) {
      pos_ = at;
      return fail();
    }
    return {};
  }

  Step root() {
    if (auto t = start_tag("stations"); not t) {
      return t;
    }
    const auto close = attributes(doc_.root);
    if (not close) {
      return std::unexpected{close.error()};
    }
    if (*close == Close::empty) {
      return {};
    }
    return content();
  }

  Step station() {
    if (doc_.stations.size() % stop_poll_stations == 0 and
        ctx_.stop.stop_requested()) {
      return std::unexpected{Error{Cancelled{}}};
    }
    if ((doc_.stations.size() + 1) >
        ctx_.limits.max_elements / elements_per_station) {
      return fail(ParseErrc::too_large);
    }
    Element element{.line = line(), .attributes = {}};
    if (auto t = start_tag("station"); not t) {
      return t;
    }
    const auto close = attributes(element.attributes);
    if (not close) {
      return std::unexpected{close.error()};
    }
    if (*close != Close::empty) {
      return fail();  // a station element with content
    }
    doc_.stations.push_back(std::move(element));
    return {};
  }

  // The root's content, up to and including its end tag.
  Step content() {
    while (true) {
      skip_space();
      if (consume("</")) {
        const std::size_t at = pos_;
        auto n = name();
        if (not n or *n != "stations") {
          pos_ = at;
          return fail();
        }
        skip_space();
        return consume(">") ? Step{} : fail();
      }
      Step s = consume("<!--") ? comment() : station();
      if (not s) {
        return s;
      }
    }
  }

  std::string_view text_;
  const ReadContext& ctx_;
  std::size_t pos_ = 0;
  std::size_t counted_ = 0;
  std::size_t line_ = 1;
  Document doc_;
};

}  // namespace

std::expected<Document, Error> scan(std::string_view text,
                                    const ReadContext& ctx) {
  if (text.size() > ctx.limits.max_text_bytes) {
    return std::unexpected{
        Error{ParseError::make(ParseErrc::too_large, {.line = 1},
                               std::to_string(text.size()) + " bytes, limit " +
                                   std::to_string(ctx.limits.max_text_bytes))}};
  }
  if (ctx.stop.stop_requested()) {
    return std::unexpected{Error{Cancelled{}}};
  }
  return Scanner{text, ctx}.scan();
}

}  // namespace mov::io::detail::ndbc_xml
