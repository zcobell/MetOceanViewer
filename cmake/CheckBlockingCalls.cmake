# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# cmake -D "MOV_REPO=<root>" -P CheckBlockingCalls.cmake
#
# Fails if a C++ file under <root>/src or <root>/tests blocks a thread or
# spins a nested event loop (plan §2.3 and §7, docs/providers-design.md
# §2.2): work is chained with QFuture::then and QPromise, and the only event
# loops are the one `exec()` of each program's main() and the test driver
# mov::test::drive_until (tests/support/include/mov/test/qt_drive.hpp),
# which may use all of them. One line elsewhere may pause: io's Windows
# atomic rename between its retries, marked `// gate: bounded-retry`.
#
# Only src/ and tests/ are scanned, on purpose: they hold all v5 C++. The
# legacy v4 trees block by design and are frozen (plan §6 decision 4), and
# tools/ holds no C++. The gates' own fixtures (tests/cmake/) are skipped.
# The rules apply to every layer, core and io included, on purpose: those
# are single-threaded pure functions over values (CLAUDE.md), so a wait or a
# QFuture read there is a design error too, and a false positive (a method
# of our own named result()) is cheaper to rename than an exemption to keep.
# The exec rule also hits QSqlQuery::exec, which does not block on an event
# loop (QDialog::exec does): code that needs it gets an exemption here.
# Registered as the ctest test no_blocking_calls.

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
# Qt's synchronous waits (QNetworkReply, QThreadPool, QIODevice, QProcess,
# QTcpServer: waitForFinished, waitForDone, waitForReadyRead, ...) and
# QTest's (qWait, qWaitFor, qWaitForWindowExposed, qSleep, QTRY_*).
set(wait_for_regex "waitFor[A-Z]|qWait|qSleep|QTRY_")
# QThread::sleep/msleep/usleep, std::this_thread::sleep_for/sleep_until,
# POSIX sleep/usleep/nanosleep, Win32 Sleep.
set(sleep_regex "${start}(m|u|nano)?[Ss]leep(_for|_until)?[ \t]*\\(")
# Reading a QFuture blocks until it is ready.
set(future_result_regex "([.>]|::)[ \t]*(result|results|resultAt|takeResult)[ \t]*\\(")
# QThread::wait, condition variables, latches, barriers, semaphores,
# atomics, std::future::wait.
set(blocking_wait_regex
    "([.>]|::)[ \t]*(wait|wait_for|wait_until|arrive_and_wait|acquire|try_acquire_for|try_acquire_until)[ \t]*\\("
)
set(synchronizer_regex "QFutureSynchronizer|QSemaphore|QtConcurrent::blocking")
# std::future::get blocks; QFuture and QPromise replace the std types.
set(std_future_regex "std::(future|shared_future|async|promise|packaged_task)${end}")
# Threads and their synchronization, as types (std::thread::id and
# hardware_concurrency stay usable): work runs on QtConcurrent's pool.
set(std_thread_regex
    "std::(j?thread|latch|barrier|counting_semaphore|binary_semaphore|condition_variable(_any)?)($|[^A-Za-z0-9_:])"
)
# QNetworkAccessManager answering on the calling thread.
set(sync_request_regex "SynchronousRequestAttribute")
# A signal that blocks its sender until the receiver's thread ran the slot.
set(blocking_connection_regex "BlockingQueuedConnection")
# QCoreApplication::exec, QEventLoop::exec, QDialog::exec, QSqlQuery::exec.
set(exec_regex "${start}exec[ \t]*\\(")
set(rules
    event_loop
    wait_for
    sleep
    future_result
    blocking_wait
    synchronizer
    std_future
    std_thread
    sync_request
    blocking_connection
    exec
)
foreach(rule IN LISTS rules)
    set(${rule}_allowed "${driver}")
endforeach()
# io's projection test starts threads to check that each gets its own PROJ
# context (docs/core-design.md §5.9).
set(std_thread_allowed "${driver}|^tests/io/test_projection\\.cpp$")
# Each program's main() runs its event loop once.
set(exec_allowed "${driver}|^src/[^/]+/main\\.cpp$")
# The Windows atomic rename retries a file another process holds open, with a
# bounded pause (docs/core-design.md §4.4) on a worker thread: only its
# marked line.
set(sleep_marker "// gate: bounded-retry")
set(sleep_marker_paths "^src/io/atomic_file\\.cpp$")

mov_scan_banned(violations ROOT "${MOV_REPO}" DIRS src tests EXCLUDE "^tests/cmake/" RULES ${rules})

if(violations)
    message(
        FATAL_ERROR
        "Blocking call or nested event loop. Chain work with QFuture::then; a test runs the event loop "
        "only through mov::test::drive_until (tests/support/include/mov/test/qt_drive.hpp); see "
        "docs/providers-design.md §2.2.\n  ${violations}"
    )
endif()
message(STATUS "No blocking calls or nested event loops under ${MOV_REPO}/src and ${MOV_REPO}/tests")
