// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// One violation per line; run.cmake expects each to be reported.
#include <QEventLoop>

void violations() {
  QCoreApplication::processEvents();
  QCoreApplication::sendPostedEvents();
  reply->waitForFinished();
  pool.waitForDone();
  QThread::msleep(10);
  std::this_thread::sleep_for(1s);
  auto value = future.result();
  auto taken = future.takeResult();
  thread->wait();
  QFutureSynchronizer<void> synchronizer;
  QSemaphore semaphore;
  QtConcurrent::blockingMap(items, work);
  std::future<int> pending = std::async(work);
  dialog.exec();
}
