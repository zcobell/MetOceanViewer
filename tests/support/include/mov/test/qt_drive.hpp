// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// The one place a test runs Qt's event loop (the no_blocking_calls gate
// allows it here and nowhere else). Catch2's main runs no loop, so the loop
// here is the top-level one, never a nested one. Qt tests only.
// docs/providers-design.md §10.2 adds drive(QFuture<T>, timeout) (P7).

#include <QEventLoop>
#include <QObject>
#include <QTimer>
#include <chrono>
#include <concepts>

namespace mov::test {

/// Runs the event loop until `done()` returns true or `timeout` has passed,
/// and returns whether `done()` returned true. `done` is called once before
/// the loop starts, then every 10 ms; on timeout it is called once more.
template <std::predicate P>
[[nodiscard]] bool drive_until(P done, std::chrono::milliseconds timeout) {
  using namespace std::chrono_literals;
  if (done()) {
    return true;
  }
  bool satisfied = false;
  QEventLoop loop;
  QTimer poll;
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
    if (done()) {
      satisfied = true;
      loop.quit();
    }
  });
  QTimer deadline;
  deadline.setSingleShot(true);
  QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
  poll.start(10ms);
  deadline.start(timeout);
  loop.exec();
  return satisfied or done();
}

}  // namespace mov::test
