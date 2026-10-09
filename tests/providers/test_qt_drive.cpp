// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// mov::test::drive_until, the event loop of the Qt tests. Outcomes only, no
// durations (docs/providers-design.md §10.2: no timing assertions).

#include <QObject>
#include <QTimer>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <stdexcept>

#include "mov/test/qt_drive.hpp"

using namespace std::chrono_literals;

TEST_CASE("drive_until runs the loop until the condition holds",
          "[providers][drive]") {
  const QObject context;
  bool fired = false;
  QTimer::singleShot(20ms, &context, [&fired] { fired = true; });
  CHECK(mov::test::drive_until([&fired] { return fired; }, 60s));
  CHECK(fired);
}

TEST_CASE("drive_until reports a condition that never holds",
          "[providers][drive]") {
  int polls = 0;
  CHECK(not mov::test::drive_until(
      [&polls] {
        ++polls;
        return false;
      },
      50ms));
  // Once before the loop, at least once in it, once after the deadline.
  CHECK(polls >= 3);
}

TEST_CASE("drive_until does not run the loop for a condition that holds",
          "[providers][drive]") {
  const QObject context;
  bool fired = false;
  QTimer::singleShot(0ms, &context, [&fired] { fired = true; });
  CHECK(mov::test::drive_until([] { return true; }, 60s));
  CHECK(not fired);
  // Leave nothing pending for the next test.
  CHECK(mov::test::drive_until([&fired] { return fired; }, 60s));
}

TEST_CASE("drive_until rethrows what the condition throws after the loop",
          "[providers][drive]") {
  int polls = 0;
  const auto throws_on_second_poll = [&polls] {
    if (++polls == 2) {
      throw std::runtime_error("condition failed");
    }
    return false;
  };
  CHECK_THROWS_AS(
      static_cast<void>(mov::test::drive_until(throws_on_second_poll, 60s)),
      std::runtime_error);
  CHECK(polls == 2);
  // The loop has exited: a second drive works.
  CHECK(mov::test::drive_until([] { return true; }, 60s));
}

TEST_CASE("drive_until refuses to nest", "[providers][drive]") {
  const auto nested = [] {
    return mov::test::drive_until([] { return false; }, 10ms);
  };
  CHECK_THROWS_AS(static_cast<void>(mov::test::drive_until(nested, 60s)),
                  std::logic_error);
  CHECK(mov::test::drive_until([] { return true; }, 60s));
}
