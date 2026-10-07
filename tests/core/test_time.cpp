// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <string>

#include "mov/core/time.hpp"

namespace {

// Sets TZ for the lifetime of the object and restores it afterwards.
class ScopedTimeZone {
 public:
  explicit ScopedTimeZone(const char* zone) {
    if (const char* old = std::getenv("TZ")) {  // NOLINT(concurrency-mt-unsafe)
      previous_ = old;
    }
    set(zone);
  }
  ScopedTimeZone(const ScopedTimeZone&) = delete;
  ScopedTimeZone& operator=(const ScopedTimeZone&) = delete;
  ScopedTimeZone(ScopedTimeZone&&) = delete;
  ScopedTimeZone& operator=(ScopedTimeZone&&) = delete;
  ~ScopedTimeZone() { set(previous_ ? previous_->c_str() : nullptr); }

 private:
  static void set(const char* zone) {
#ifdef _WIN32
    _putenv_s("TZ", zone ? zone : "");
    _tzset();
#else
    if (zone != nullptr) {
      ::setenv("TZ", zone, 1);
    } else {
      ::unsetenv("TZ");
    }
    ::tzset();
#endif
  }

  std::optional<std::string> previous_;
};

}  // namespace

// N5: v4 parsed the ADCIRC cold start without a time spec, so the result
// depended on the machine's zone and DST rules.
TEST_CASE("parse_utc_datetime is independent of the TZ environment",
          "[core][time][regression][N5]") {
  using namespace std::chrono;
  // 2005-08-28 12:00 UTC (Katrina) is 07:00 in Chicago, during DST.
  const mov::core::Time expected =
      time_point_cast<milliseconds>(sys_days{year{2005} / August / day{28}}) +
      hours{12};
  for (const char* zone :
       {"UTC", "America/Chicago", "Asia/Kolkata", "Pacific/Auckland"}) {
    const ScopedTimeZone scoped{zone};
    INFO("TZ=" << zone);
    CHECK(mov::core::parse_utc_datetime("2005-08-28 12:00:00") == expected);
    CHECK(mov::core::parse_utc_datetime("2005-08-28T12:00:00Z") == expected);
  }
}
