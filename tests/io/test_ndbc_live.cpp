// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Live probes of the NDBC file rules (label live; they skip unless
// MOV_LIVE_API=1; docs/providers-design.md section 10.5): the id case of
// each directory for ids of four, five and seven characters, and the month
// codes of finalized months, October to December included once one is
// finalized (L16; section 1.3 lists the letters a-c as unverified). The
// requests are io's targets, sent with Qt Network directly: the providers
// (P7) are not built yet. An unreachable origin skips; a changed rule fails.

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>

#include "mov/core/provider/ndbc.hpp"
#include "mov/io/part.hpp"
#include "mov/io/read.hpp"
#include "mov/io/wire/ndbc.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/test/live.hpp"
#include "mov/test/qt_drive.hpp"

namespace {

namespace wn = mov::io::wire::ndbc;
using mov::core::ndbc::FileKind;
using std::chrono::month;
using std::chrono::year;

struct Response {
  int status = 0;
  QByteArray body;
};

constexpr std::chrono::seconds request_timeout{60};
constexpr int http_ok = 200;
constexpr int http_not_found = 404;

// GET of a path on the NDBC origin; skips when the origin cannot be reached.
Response get(QNetworkAccessManager& network, std::string_view path) {
  const std::string url =
      std::string{mov::io::wire::base(mov::io::wire::Origin::ndbc)} +
      std::string{path};
  QNetworkRequest request{QUrl{QString::fromStdString(url)}};
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("MetOceanViewer-live-tests"));
  request.setTransferTimeout(
      static_cast<int>(std::chrono::milliseconds{request_timeout}.count()));
  const std::unique_ptr<QNetworkReply> reply{network.get(request)};
  const bool finished = mov::test::drive_until(
      [&reply] { return reply->isFinished(); }, request_timeout);
  Response out;
  out.status =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  out.body = reply->readAll();
  if (not finished or out.status == 0) {
    SKIP("NDBC unreachable: " << url);
  }
  return out;
}

Response get(QNetworkAccessManager& network, std::string_view id,
             const wn::File& file) {
  const auto target = wn::file_target(wn::Station::make(id).value(), file);
  REQUIRE(target.has_value());
  return get(network, target->path);
}

// The body read as a standard-met file: at least one column.
void check_parses(const Response& r, FileKind kind) {
  const auto bytes = std::as_bytes(
      std::span{r.body.constData(), static_cast<std::size_t>(r.body.size())});
  const auto reply = wn::read_stdmet(bytes, "", kind, {});
  REQUIRE(reply.has_value());
  const auto* part = std::get_if<mov::io::Read<mov::io::Part>>(&*reply);
  REQUIRE(part != nullptr);
  CHECK(not part->value.columns().empty());
}

}  // namespace

TEST_CASE("ids of four, five and seven characters, by each directory's case",
          "[io][wire][ndbc][live]") {
  mov::test::require_live_api();
  QNetworkAccessManager network;
  // realtime2: upper case only.
  for (const std::string_view id : {"KATP", "41002"}) {
    INFO(id);
    const Response r = get(network, id, wn::Realtime{});
    CHECK(r.status == http_ok);
    check_parses(r, FileKind::realtime);
  }
  CHECK(get(network, "/data/realtime2/katp.txt").status == http_not_found);
  // A seven-digit station serves realtime2 while it reports (2026-10-16).
  const Response seven = get(network, "4403585", wn::Realtime{});
  if (seven.status == http_ok) {
    check_parses(seven, FileKind::realtime);
  } else {
    WARN("realtime2/4403585.txt: HTTP " << seven.status);
  }
  // Historical: lower case only; a four-character id of the 1970s.
  const Response old = get(network, "EB01", wn::Historical{.year = year{1970}});
  CHECK(old.status == http_ok);
  check_parses(old, FileKind::historical);
  CHECK(get(network, "/data/historical/stdmet/EB01h1970.txt.gz").status ==
        http_not_found);
}

namespace {

// A finalized month of 41002: its letter spelling, else (October to
// December only) its decimal one. Returns the spelling that served it.
std::string probe_month(QNetworkAccessManager& network,
                        std::chrono::year_month ym) {
  const Response letter =
      get(network, "41002",
          wn::Monthly{.month = ym, .spelling = wn::MonthSpelling::letter});
  if (letter.status == http_ok) {
    check_parses(letter, FileKind::monthly);
    return "letter";
  }
  constexpr unsigned october = 10;
  REQUIRE(static_cast<unsigned>(ym.month()) >= october);
  const Response decimal =
      get(network, "41002",
          wn::Monthly{.month = ym, .spelling = wn::MonthSpelling::decimal});
  REQUIRE(decimal.status == http_ok);
  check_parses(decimal, FileKind::monthly);
  return "decimal";
}

}  // namespace

TEST_CASE("the month codes of finalized months",
          "[io][wire][ndbc][live][regression][L16]") {
  mov::test::require_live_api();
  QNetworkAccessManager network;
  const std::chrono::year_month_day today{
      std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())};
  const auto current = static_cast<unsigned>(today.month());
  // A month is finalized some time after it ends: this year's months up to
  // two back.
  for (unsigned m = 1; m + 2 <= current; ++m) {
    INFO("month " << m);
    CHECK(probe_month(network, today.year() / month{m}) == "letter");
  }
  // Until last year's h{yyyy} is published its months are monthly files, so
  // October to December can be checked then.
  const year last_year = today.year() - std::chrono::years{1};
  if (get(network, "41002", wn::Historical{.year = last_year}).status ==
      http_not_found) {
    for (unsigned m = 10; m <= 12; ++m) {
      const std::string spelling = probe_month(network, last_year / month{m});
      INFO("month " << m << " of last year: " << spelling);
      CHECK(spelling == "letter");  // decimal: update section 1.3
    }
  } else {
    WARN(
        "October to December are not finalized monthly files now: the "
        "letter codes a-c stay unverified");
  }
}
