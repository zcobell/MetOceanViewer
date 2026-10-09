// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// The one place a test runs Qt's event loop (the no_blocking_calls gate
// allows it here and nowhere else; docs/providers-design.md §2.2, §10.2).
// Catch2's main runs no loop, so the loop here is the top-level one, and
// drive_until refuses to be nested in itself. Qt tests only.

#include <QEventLoop>
#include <QObject>
#include <QTimer>
#include <chrono>
#include <concepts>
#include <exception>
#include <stdexcept>

namespace mov::test {

namespace detail {
// How many drive_until calls are running on this thread: 0 or 1.
inline thread_local int drive_depth = 0;

class DriveDepth {
 public:
  DriveDepth() {
    if (drive_depth != 0) {
      throw std::logic_error(
          "mov::test::drive_until called while it runs: a nested event loop");
    }
    ++drive_depth;
  }
  DriveDepth(const DriveDepth&) = delete;
  DriveDepth& operator=(const DriveDepth&) = delete;
  DriveDepth(DriveDepth&&) = delete;
  DriveDepth& operator=(DriveDepth&&) = delete;
  ~DriveDepth() { --drive_depth; }
};
}  // namespace detail

/// Runs the event loop until `done()` returns true or `timeout` has passed,
/// and returns whether `done()` returned true. `done` is called once before
/// the loop starts, then every 10 ms; on timeout it is called once more. An
/// exception from `done` ends the loop and is rethrown here, after it.
/// Throws std::logic_error when called from inside another drive_until.
template <std::predicate P>
[[nodiscard]] bool drive_until(P done, std::chrono::milliseconds timeout) {
  using namespace std::chrono_literals;
  const detail::DriveDepth depth;
  if (done()) {
    return true;
  }
  bool satisfied = false;
  std::exception_ptr failure;
  QEventLoop loop;
  QTimer poll;
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
    try {
      satisfied = done();
    } catch (...) {
      failure = std::current_exception();
    }
    if (satisfied or failure) {
      poll.stop();
      loop.quit();
    }
  });
  QTimer deadline;
  deadline.setSingleShot(true);
  QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
  poll.start(10ms);
  deadline.start(timeout);
  loop.exec();
  if (failure) {
    std::rethrow_exception(failure);
  }
  return satisfied or done();
}

}  // namespace mov::test
