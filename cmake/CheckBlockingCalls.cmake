# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# cmake -D "MOV_REPO=<root>" -P CheckBlockingCalls.cmake
#
# Fails if a C++ file under <root>/src or <root>/tests calls something that
# blocks a thread or spins a nested event loop (plan §2.3 and §7,
# docs/providers-design.md §2.2): work is chained with QFuture::then and
# QPromise, and the only event loops are the one `exec()` of each program's
# main() and the test driver mov::test::drive
# (tests/support/include/mov/test/qt_drive.hpp), which may use all of them.
# One more exception: io's Windows atomic rename pauses between its retries.
# The gates' own fixtures (tests/cmake/) are not scanned. Registered as the
# ctest test no_blocking_calls.

cmake_minimum_required(VERSION 4.4)

include("${CMAKE_CURRENT_LIST_DIR}/SourceScan.cmake")

if(NOT MOV_REPO)
    message(FATAL_ERROR "MOV_REPO is not set")
endif()

# A name starts (ends) here: not preceded (followed) by an identifier character.
set(start "(^|[^A-Za-z0-9_])")
set(end "($|[^A-Za-z0-9_])")
set(driver "^tests/support/include/mov/test/qt_drive\\.hpp$")

# Nested event loops.
set(event_loop_regex "QEventLoop|processEvents|sendPostedEvents")
# Qt's synchronous waits: QNetworkReply, QThreadPool, QIODevice, QProcess,
# QTcpServer ... (waitForFinished, waitForDone, waitForReadyRead, ...) and
# QTest::qWait, qWaitFor, qWaitForWindowExposed.
set(wait_for_regex "waitFor[A-Z]|qWait")
# QThread::sleep/msleep/usleep, std::this_thread::sleep_for/sleep_until,
# POSIX sleep/usleep/nanosleep, Win32 Sleep.
set(sleep_regex "${start}(m|u|nano)?[Ss]leep(_for|_until)?[ \t]*\\(")
# Reading a QFuture blocks until it is ready.
set(future_result_regex "([.>]|::)[ \t]*(result|results|resultAt|takeResult)[ \t]*\\(")
# QThread::wait, condition variables, latches, atomics, std::future::wait.
set(blocking_wait_regex "([.>]|::)[ \t]*wait[ \t]*\\(")
set(synchronizer_regex "QFutureSynchronizer|QSemaphore|QtConcurrent::blocking")
# std::future::get blocks; QFuture and QPromise replace the std types.
set(std_future_regex "std::(future|shared_future|async|promise|packaged_task)${end}")
# QCoreApplication::exec, QEventLoop::exec, QDialog::exec, ...
set(exec_regex "${start}exec[ \t]*\\(")
set(rules
    event_loop
    wait_for
    sleep
    future_result
    blocking_wait
    synchronizer
    std_future
    exec
)
foreach(rule IN LISTS rules)
    set(${rule}_allowed "${driver}")
endforeach()
# Each program's main() runs its event loop once.
set(exec_allowed "${driver}|^src/[^/]+/main\\.cpp$")
# The Windows atomic rename retries a file another process holds open, with a
# bounded pause (docs/core-design.md §4.4); io runs on worker threads only.
set(sleep_allowed "${driver}|^src/io/atomic_file\\.cpp$")

mov_scan_banned(violations ROOT "${MOV_REPO}" DIRS src tests EXCLUDE "^tests/cmake/" RULES ${rules})

if(violations)
    message(
        FATAL_ERROR
        "Blocking call or nested event loop (chain with QFuture::then; tests use mov::test::drive):\n  ${violations}"
    )
endif()
message(STATUS "No blocking calls or nested event loops under ${MOV_REPO}/src and ${MOV_REPO}/tests")
