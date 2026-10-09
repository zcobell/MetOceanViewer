// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// mov::test::drive_until, the event loop of the Qt tests. Outcomes only, no
// durations (docs/providers-design.md §10.2: no timing assertions).

#include <QTimer>
#include <catch2/catch_test_macros.hpp>
#include <chrono>

#include "mov/test/qt_drive.hpp"

using namespace std::chrono_literals;

TEST_CASE("drive_until runs the loop until the condition holds",
          "[providers][drive]") {
  bool fired = false;
  QTimer::singleShot(20ms, [&fired] { fired = true; });
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
  CHECK(polls >= 2);
}

TEST_CASE("drive_until does not run the loop for a condition that holds",
          "[providers][drive]") {
  bool fired = false;
  QTimer::singleShot(0ms, [&fired] { fired = true; });
  CHECK(mov::test::drive_until([] { return true; }, 60s));
  CHECK(not fired);
  // Leave nothing pending for the next test.
  CHECK(mov::test::drive_until([&fired] { return fired; }, 60s));
}
