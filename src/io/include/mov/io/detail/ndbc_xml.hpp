// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The XML subset scanner behind ndbc::read_active_stations
// (docs/providers-design.md section 4.5). Private to mov::io (public only
// because the tests, the fuzz target and the QXmlStreamReader reference test
// include it).

#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"

namespace mov::io::detail::ndbc_xml {

/// An attribute as XML reports it: its name, and its value with the
/// entity and character references replaced.
struct Attribute {
  std::string name;
  std::string value;
  friend bool operator==(const Attribute&, const Attribute&) = default;
};

/// A `station` element: the line its `<` is on (1-based) and its
/// attributes in document order.
struct Element {
  std::size_t line;
  std::vector<Attribute> attributes;
  friend bool operator==(const Element&, const Element&) = default;
};

/// What the scanner accepted: the `stations` element's attributes and its
/// `station` children.
struct Document {
  std::vector<Attribute> root;
  std::vector<Element> stations;
  friend bool operator==(const Document&, const Document&) = default;
};

/// At most this many attributes on one element (the list has 12).
inline constexpr std::size_t max_attributes = 64;

/// What one station costs against ReadLimits::max_elements (CD section 5).
inline constexpr std::size_t elements_per_station = 16;

/// Scans `text` as the subset of XML 1.0 that activestations.xml is
/// written in, and refuses everything else (fails closed):
///
///   document   ::= BOM? XMLDecl? Misc* root Misc*
///   XMLDecl    ::= '<?xml' (S name Eq '"'value'"')+ S? '?>'
///                  version="1.0", then optionally encoding="UTF-8" (any
///                  case), then optionally standalone="yes"|"no"; either
///                  quote
///   Misc       ::= S | '<!--' text without '--' '-->'
///   root       ::= '<stations' Attrs ('/>' | '>' (S | Comment | station)*
///                  '</stations' S? '>')
///   station    ::= '<station' Attrs '/>'
///   Attrs      ::= (S name S? '=' S? quoted)* S?
///   name       ::= [A-Za-z_][A-Za-z0-9_.-]*, not starting with "xml" in
///                  any case (no namespaces, no reserved names); unique in
///                  its element
///   quoted     ::= '"' ... '"' | "'" ... "'": no '<', no byte below
///                  0x20 (XML would turn a literal tab, CR or LF into a
///                  space), '&' only as &lt; &gt; &amp; &quot; &apos;
///                  &#digits; or &#xhex; of an XML character
///
/// The whole text must be UTF-8 of XML characters (no control byte but tab,
/// CR and LF, no U+FFFE or U+FFFF). Errors: ParseError `corrupt_record` at
/// the first byte outside the subset (context: the text from there),
/// `empty_input` for a blank text, `too_large` (the text over
/// max_text_bytes, more than max_attributes on an element, or more stations
/// than max_elements / elements_per_station); Cancelled (polled every 4096
/// stations).
[[nodiscard]] std::expected<Document, Error> scan(std::string_view text,
                                                  const ReadContext& ctx);

}  // namespace mov::io::detail::ndbc_xml
