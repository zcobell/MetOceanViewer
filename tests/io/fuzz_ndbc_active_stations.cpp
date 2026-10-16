// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: the activestations.xml subset scanner and
// ndbc::read_active_stations (docs/providers-design.md section 10.4).
// Laws:
//  - no crash, UB or hang;
//  - what the scanner accepts it accepts again written canonically, with
//    the same attributes (format, then scan, is the identity), so the
//    subset is closed and its values are decoded exactly once;
//  - every accepted attribute name follows the subset's grammar, unique in
//    its element; station lines never decrease;
//  - the records are the stations minus those counted skipped, with valid
//    and distinct ids;
//  - a refusal is a ParseError whose line lies inside the text.
// The comparison with a real XML reader (QXmlStreamReader) runs over this
// target's seeds in test_ndbc_xml_reference.cpp (label qt).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/ascii.hpp"
#include "mov/io/detail/ndbc_xml.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/ndbc.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

namespace xml = mov::io::detail::ndbc_xml;
namespace wn = mov::io::wire::ndbc;

[[noreturn]] void fail() { std::abort(); }

std::string escaped(std::string_view value) {
  std::string out;
  for (const char c : value) {
    switch (c) {
      case '<':
        out += "&lt;";
        break;
      case '&':
        out += "&amp;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        // A tab, CR or LF came from a character reference: written as one
        // again (literal, the subset refuses it).
        if (static_cast<unsigned char>(c) < ' ') {
          out += "&#" + std::to_string(static_cast<int>(c)) + ";";
        } else {
          out.push_back(c);
        }
    }
  }
  return out;
}

void write_attributes(std::string& out,
                      const std::vector<xml::Attribute>& attributes) {
  for (const auto& a : attributes) {
    out += " " + a.name + "=\"" + escaped(a.value) + "\"";
  }
}

std::string canonical(const xml::Document& d) {
  std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<stations";
  write_attributes(out, d.root);
  out += ">\n";
  for (const auto& s : d.stations) {
    out += "<station";
    write_attributes(out, s.attributes);
    out += "/>\n";
  }
  return out + "</stations>\n";
}

bool name_ok(std::string_view name) {
  return not name.empty() and
         (mov::core::ascii::is_alpha(name.front()) or name.front() == '_') and
         std::ranges::all_of(name,
                             [](char c) {
                               return mov::core::ascii::is_alnum(c) or
                                      c == '_' or c == '.' or c == '-';
                             }) and
         not mov::core::ascii::equal_ignore_case(name.substr(0, 3), "xml");
}

void check_attributes(const std::vector<xml::Attribute>& attributes) {
  std::set<std::string_view> names;
  for (const auto& a : attributes) {
    if (not name_ok(a.name) or not names.insert(a.name).second) {
      fail();
    }
  }
}

void check_document(const xml::Document& d) {
  check_attributes(d.root);
  std::size_t line = 1;
  for (const auto& s : d.stations) {
    check_attributes(s.attributes);
    if (s.line < line) {
      fail();
    }
    line = s.line;
  }
  const auto again = xml::scan(canonical(d), mov::io::ReadContext{});
  if (not again or not(again->root == d.root) or
      again->stations.size() != d.stations.size()) {
    fail();
  }
  for (std::size_t i = 0; i < d.stations.size(); ++i) {
    if (not(again->stations[i].attributes == d.stations[i].attributes)) {
      fail();
    }
  }
}

void check_records(const mov::io::Read<std::vector<wn::StationRecord>>& r,
                   std::size_t stations) {
  std::size_t skipped = 0;
  for (const auto& w : r.warnings) {
    skipped += w.code == mov::io::WarningCode::station_skipped ? w.count : 0;
  }
  if (r.value.size() + skipped != stations) {
    fail();
  }
  std::set<std::string_view> ids;
  for (const auto& record : r.value) {
    const auto id = wn::Station::make(record.station.id.value());
    if (not id or not(*id == record.station.id) or
        not ids.insert(record.station.id.value()).second) {
      fail();
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string storage(data, data + size);
  const std::string_view text{storage};
  mov::io::ReadContext ctx;
  ctx.limits.max_elements = std::size_t{1} << 16;

  const auto document = xml::scan(text, ctx);
  const auto records = wn::read_active_stations(text, ctx);
  if (document.has_value() != records.has_value()) {
    fail();
  }
  if (document) {
    check_document(*document);
    check_records(*records, document->stations.size());
    return 0;
  }
  const auto* error = std::get_if<mov::io::ParseError>(&document.error());
  const auto lines =
      static_cast<std::size_t>(std::ranges::count(text, '\n')) + 1;
  if (error == nullptr or error->line() < 1 or error->line() > lines) {
    fail();
  }
  return 0;
}
