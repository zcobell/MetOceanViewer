// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <string>

#include "mov/core/time.hpp"

namespace {

// The value of an environment variable, if set. std::getenv is C4996
// (deprecated) with MSVC, which the warning level turns into an error.
std::optional<std::string> get_env(const char* name) {
#ifdef _WIN32
  char* buffer = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&buffer, &size, name) != 0 or buffer == nullptr) {
    return std::nullopt;
  }
  std::string value{buffer};
  std::free(buffer);  // NOLINT(cppcoreguidelines-no-malloc,hicpp-no-malloc)
  return value;
#else
  const char* value = std::getenv(name);  // NOLINT(concurrency-mt-unsafe)
  return value != nullptr ? std::optional<std::string>{value} : std::nullopt;
#endif
}

// Sets TZ for the lifetime of the object and restores it afterwards.
class ScopedTimeZone {
 public:
  explicit ScopedTimeZone(const char* zone) : previous_{get_env("TZ")} {
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

// v4 parsed the ADCIRC cold start without a time spec, so the result
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
