// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The activestations.xml subset scanner against a real XML reader,
// QXmlStreamReader (docs/providers-design.md sections 4.5 and 10.4): every
// text the scanner accepts, the reference accepts too, with the same root
// and station attributes. The texts are the recorded lists, the fuzz seeds,
// and mutants of small documents. (The reverse does not hold: the scanner
// refuses much that is XML, by design.)

#include <QByteArray>
#include <QString>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mov/io/detail/ndbc_xml.hpp"
#include "mov/io/gzip.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/test/fixture.hpp"
#include "mov/test/scratch_dir.hpp"

namespace {

namespace xml = mov::io::detail::ndbc_xml;

// What the reference reader made of a text: nullopt when it is not XML, or
// when its shape is not a `stations` root of `station` children (the scanner
// accepts nothing else, so then it must have refused).
std::optional<xml::Document> reference(std::string_view text) {
  QXmlStreamReader reader{
      QByteArray{text.data(), static_cast<qsizetype>(text.size())}};
  xml::Document out;
  int depth = 0;
  bool shape_ok = true;
  const auto attributes = [&reader] {
    std::vector<xml::Attribute> list;
    for (const auto& a : reader.attributes()) {
      list.push_back({.name = a.qualifiedName().toString().toStdString(),
                      .value = a.value().toString().toStdString()});
    }
    return list;
  };
  while (not reader.atEnd()) {
    switch (reader.readNext()) {
      case QXmlStreamReader::StartElement:
        ++depth;
        if (depth == 1 and reader.name() == QStringLiteral("stations")) {
          out.root = attributes();
        } else if (depth == 2 and reader.name() == QStringLiteral("station")) {
          out.stations.push_back(
              {.line = static_cast<std::size_t>(reader.lineNumber()),
               .attributes = attributes()});
        } else {
          shape_ok = false;
        }
        break;
      case QXmlStreamReader::EndElement:
        --depth;
        break;
      case QXmlStreamReader::Characters:
        shape_ok = shape_ok and reader.isWhitespace();
        break;
      case QXmlStreamReader::DTD:
      case QXmlStreamReader::EntityReference:
      case QXmlStreamReader::ProcessingInstruction:
        shape_ok = false;
        break;
      default:
        break;
    }
  }
  if (reader.hasError() or not shape_ok) {
    return std::nullopt;
  }
  return out;
}

// A scanner verdict that agrees with the reference: an accepted text is
// XML of the same root and stations (lines aside: the reference reports
// where an element ends).
void check_against_reference(std::string_view text) {
  const auto ours = xml::scan(text, mov::io::ReadContext{});
  if (not ours) {
    return;
  }
  const auto theirs = reference(text);
  INFO("text: " << std::string{text});
  REQUIRE(theirs.has_value());
  const xml::Document reference_document = theirs.value_or(xml::Document{});
  CHECK(ours->root == reference_document.root);
  REQUIRE(ours->stations.size() == reference_document.stations.size());
  for (std::size_t i = 0; i < ours->stations.size(); ++i) {
    CHECK(ours->stations[i].attributes ==
          reference_document.stations[i].attributes);
  }
}

// splitmix64: a reproducible stream for the mutants.
class Stream {
 public:
  explicit Stream(std::uint64_t state) : state_{state} {}
  std::uint64_t next() {
    std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
  }
  std::size_t below(std::size_t n) { return next() % n; }

 private:
  std::uint64_t state_;
};

constexpr std::array<std::string_view, 34> pieces{"<",
                                                  ">",
                                                  "/",
                                                  "/>",
                                                  "!",
                                                  "?",
                                                  "-",
                                                  "--",
                                                  "&",
                                                  "#",
                                                  "x",
                                                  ";",
                                                  "\"",
                                                  "'",
                                                  "=",
                                                  " ",
                                                  "\t",
                                                  "\n",
                                                  "\r\n",
                                                  "a",
                                                  "station",
                                                  "stations",
                                                  "<!--",
                                                  "-->",
                                                  "&amp;",
                                                  "&#65;",
                                                  "&#x10FFFF;",
                                                  "\xC3\xA9",
                                                  "\xFF",
                                                  "xmlns",
                                                  "<?xml version=\"1.0\"?>",
                                                  "<![CDATA[",
                                                  "<station id=\"1\"/>",
                                                  "</stations>"};

std::string mutant(std::string text, Stream& s) {
  const std::size_t edits = 1 + s.below(3);
  for (std::size_t e = 0; e < edits; ++e) {
    const std::size_t at = s.below(text.size() + 1);
    switch (s.below(3)) {
      case 0:
        text.insert(at, pieces.at(s.below(pieces.size())));
        break;
      case 1:
        text.erase(at, s.below(4));
        break;
      default:
        if (at < text.size()) {
          text[at] = pieces.at(s.below(pieces.size())).front();
        }
        break;
    }
  }
  return text;
}

}  // namespace

TEST_CASE("the recorded lists agree with QXmlStreamReader",
          "[io][wire][ndbc][qt]") {
  const std::string trimmed =
      mov::test::read_bytes(mov::test::fixture("io/ndbc/activestations.xml"));
  check_against_reference(trimmed);
  const std::string gz = mov::test::read_bytes(
      mov::test::fixture("io/ndbc/activestations_full.xml.gz"));
  const auto full = mov::io::gunzip(
      std::as_bytes(std::span{gz.data(), gz.size()}), std::size_t{1} << 20);
  REQUIRE(full.has_value());
  REQUIRE(xml::scan(*full, mov::io::ReadContext{}).has_value());
  check_against_reference(*full);
}

TEST_CASE("the fuzz seeds agree with QXmlStreamReader",
          "[io][wire][ndbc][qt]") {
  std::size_t seeds = 0;
  for (const auto& entry : std::filesystem::directory_iterator{
           mov::test::fixture("io/ndbc_active_stations")}) {
    check_against_reference(mov::test::read_bytes(entry.path()));
    ++seeds;
  }
  CHECK(seeds > 0);
}

TEST_CASE("mutants agree with QXmlStreamReader", "[io][wire][ndbc][qt]") {
  constexpr std::array<std::string_view, 4> bases{
      "<?xml version=\"1.0\" encoding=\"utf-8\"?><stations created=\"x\">\n"
      "  <!-- a comment -->\n"
      "  <station id=\"41002\" lat=\"33.3\" lon=\"-76.3\" name=\"A &amp; "
      "B\"/>\n"
      "</stations>",
      "<stations><station a='&#x42;&quot;' "
      "b=\"&lt;&gt;\"/><station/></stations>",
      "\xEF\xBB\xBF<stations/>",
      "<stations\r\n count = '1'><!----></stations ><!-- end -->\n"};
  Stream s{20261015};
  constexpr std::size_t per_base = 5000;
  std::size_t accepted = 0;
  for (const std::string_view base : bases) {
    check_against_reference(base);
    for (std::size_t i = 0; i < per_base; ++i) {
      const std::string text = mutant(std::string{base}, s);
      if (xml::scan(text, mov::io::ReadContext{})) {
        ++accepted;
      }
      check_against_reference(text);
    }
  }
  // Both verdicts were exercised.
  CHECK(accepted > per_base / 10);
  CHECK(accepted < bases.size() * per_base);
}
