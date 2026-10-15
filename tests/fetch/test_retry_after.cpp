// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Retry-After and HTTP-dates (RFC 9110 §10.2.3, §5.6.7).

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <optional>
#include <string_view>

#include "mov/fetch/retry.hpp"

namespace {

using mov::fetch::parse_http_date;
using mov::fetch::retry_after;
using std::chrono::seconds;
using std::chrono::sys_days;
using std::chrono::sys_seconds;

constexpr std::string_view date = "Sun, 06 Nov 1994 08:49:37 GMT";

sys_seconds nov6_1994(int h, int m, int s) {
  return sys_seconds{
             sys_days{std::chrono::year{1994} / std::chrono::November / 6}} +
         std::chrono::hours{h} + std::chrono::minutes{m} + seconds{s};
}

}  // namespace

TEST_CASE("Retry-After as delta-seconds", "[fetch][retry-after]") {
  CHECK(retry_after("120", std::nullopt) == seconds{120});
  CHECK(retry_after(" \t5 ", std::nullopt) == seconds{5});
  CHECK(retry_after("0", std::nullopt) == seconds{1});  // at least 1 s
  CHECK(retry_after("0005", std::nullopt) == seconds{5});
  // Past 2^31 it saturates there (RFC 9111 §1.2.2).
  CHECK(retry_after("99999999999999999999999999", std::nullopt) ==
        seconds{std::int64_t{1} << 31});
}

TEST_CASE("Retry-After that is neither form is nullopt",
          "[fetch][retry-after]") {
  CHECK_FALSE(retry_after("", std::nullopt));
  CHECK_FALSE(retry_after("   ", std::nullopt));
  CHECK_FALSE(retry_after("-5", std::nullopt));
  CHECK_FALSE(retry_after("+5", std::nullopt));
  CHECK_FALSE(retry_after("1.5", std::nullopt));
  CHECK_FALSE(retry_after("5 s", std::nullopt));
  CHECK_FALSE(retry_after("soon", date));
}

TEST_CASE("Retry-After as an HTTP-date is taken relative to Date",
          "[fetch][retry-after]") {
  CHECK(retry_after("Sun, 06 Nov 1994 08:50:37 GMT", date) == seconds{60});
  // The server's clock, however skewed against ours, decides.
  CHECK(retry_after("Mon, 07 Nov 1994 08:49:37 GMT", date) == seconds{86400});
  // A date at or before Date is the minimum wait.
  CHECK(retry_after(date, date) == seconds{1});
  CHECK(retry_after("Sat, 05 Nov 1994 08:49:37 GMT", date) == seconds{1});
  // Without a usable Date there is no reference: nullopt, never the local
  // clock.
  CHECK_FALSE(retry_after("Sun, 06 Nov 1994 08:50:37 GMT", std::nullopt));
  CHECK_FALSE(retry_after("Sun, 06 Nov 1994 08:50:37 GMT", "yesterday"));
  CHECK(retry_after("Sun, 06 Nov 1994 08:50:37 GMT",
                    " Sun, 06 Nov 1994 08:49:37 GMT ") == seconds{60});
}

TEST_CASE("the three HTTP-date forms", "[fetch][retry-after]") {
  const auto expected = nov6_1994(8, 49, 37);
  CHECK(parse_http_date("Sun, 06 Nov 1994 08:49:37 GMT") == expected);
  CHECK(parse_http_date("Sunday, 06-Nov-94 08:49:37 GMT") == expected);
  CHECK(parse_http_date("Sun Nov  6 08:49:37 1994") == expected);
  CHECK(parse_http_date("Sun Nov 16 08:49:37 1994") ==
        expected + std::chrono::days{10});
  // RFC 850's two-digit years: 70 and later are 19yy, earlier 20yy.
  CHECK(parse_http_date("Monday, 06-Nov-06 08:49:37 GMT") ==
        sys_seconds{
            sys_days{std::chrono::year{2006} / std::chrono::November / 6}} +
            std::chrono::hours{8} + std::chrono::minutes{49} + seconds{37});
  // A leap second lands on the next minute.
  CHECK(parse_http_date("Sun, 06 Nov 1994 08:49:60 GMT") ==
        nov6_1994(8, 50, 0));
}

TEST_CASE("malformed HTTP-dates are nullopt", "[fetch][retry-after]") {
  for (const std::string_view bad : {
           "Sun, 06 Nov 1994 08:49:37 UTC",   // only GMT
           "Sun, 06 Nov 1994 08:49:37 GMT ",  // trailing text
           "Sun, 6 Nov 1994 08:49:37 GMT",    // two-digit day
           "Sun, 31 Nov 1994 08:49:37 GMT",   // no such day
           "Sun, 06 Nov 1994 24:00:00 GMT",   // hour
           "Sun, 06 Nov 1994 08:60:00 GMT",   // minute
           "Sun, 06 Nov 1994 08:49:61 GMT",   // second
           "Sun, 06 Foo 1994 08:49:37 GMT",   // month
           "Xyz, 06 Nov 1994 08:49:37 GMT",   // day name
           "Sun 06 Nov 1994 08:49:37 GMT",    // comma
           "Sun, 06 Nov 94 08:49:37 GMT",     // year digits
           "Sun, 06 Nov 1994 08-49-37 GMT",   // separators
           "Sun, 06 Nov 1994 08:4",           // cut short
           "Sunday, 06-Nov-1994 08:49:37 GMT",
           "Sun Nov 6 08:49:37 1994",  // asctime pads with a space
           "Sun Nov  6 08:49:37 94",
           "Sun Nov  6 08:49:37 1994 ",
           "",
       }) {
    INFO(bad);
    CHECK_FALSE(parse_http_date(bad));
  }
}
