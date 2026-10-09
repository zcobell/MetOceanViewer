// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// One violation per line; run.cmake expects each to be reported.
#include <QEventLoop>

void violations() {
  QCoreApplication::processEvents();
  QCoreApplication::sendPostedEvents();
  reply->waitForFinished();
  pool.waitForDone();
  QTest::qSleep(10);
  QTRY_VERIFY(done);
  QThread::msleep(10);
  std::this_thread::sleep_for(1s);
  auto value = future.result();
  auto taken = future.takeResult();
  thread->wait();
  changed.wait_for(lock, 1s);
  latch.arrive_and_wait();
  slots.acquire();
  slots.try_acquire_for(1s);
  QFutureSynchronizer<void> synchronizer;
  QSemaphore semaphore;
  QtConcurrent::blockingMap(items, work);
  std::future<int> pending = std::async(work);
  std::jthread worker(work);
  std::condition_variable_any changed;
  std::counting_semaphore<4> slots{4};
  request.setAttribute(QNetworkRequest::SynchronousRequestAttribute, true);
  connect(a, &A::done, b, &B::run, Qt::BlockingQueuedConnection);
  dialog.exec();
}

// Not a type use: run.cmake expects this one silent.
const unsigned cores = std::thread::hardware_concurrency();
