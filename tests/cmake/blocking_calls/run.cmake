# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# cmake -DMOV_REPO=... -P run.cmake
#
# cmake/CheckBlockingCalls.cmake must report every banned call in the tree
# here (one per line of src/providers/job.cpp, the non-exec call of a
# main.cpp, an exec() in a main.cpp below a layer directory, a QTest wait,
# an unmarked pause in src/io/atomic_file.cpp and a marked one in another
# file), must not report comments, a layer's main() exec(), the test
# driver, the marked pause or the tests/cmake fixtures, must read lines
# right (src/providers/lexing.cpp), must accept the clean tree and must fail
# on a missing root or subdirectory and on a directory without C++ files.

set(scanner "${MOV_REPO}/cmake/CheckBlockingCalls.cmake")

function(run_scanner root out_result out_output)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DMOV_REPO=${root}" -P "${scanner}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output
    )
    set(${out_result} "${result}" PARENT_SCOPE)
    set(${out_output} "${output}" PARENT_SCOPE)
endfunction()

set(failures "")

run_scanner("${CMAKE_CURRENT_LIST_DIR}" result output)
if(result EQUAL 0)
    list(APPEND failures "a tree with blocking calls was accepted")
endif()
set(expected
    "src/providers/job.cpp: [event_loop] #include <QEventLoop>"
    "src/providers/job.cpp: [event_loop] QCoreApplication::processEvents()"
    "src/providers/job.cpp: [event_loop] QCoreApplication::sendPostedEvents()"
    "src/providers/job.cpp: [wait_for] reply->waitForFinished()"
    "src/providers/job.cpp: [wait_for] pool.waitForDone()"
    "src/providers/job.cpp: [wait_for] QTest::qSleep(10)"
    "src/providers/job.cpp: [wait_for] QTRY_VERIFY(done)"
    "src/providers/job.cpp: [sleep] QThread::msleep(10)"
    "src/providers/job.cpp: [sleep] std::this_thread::sleep_for(1s)"
    "src/providers/job.cpp: [future_result] auto value = future.result()"
    "src/providers/job.cpp: [future_result] auto taken = future.takeResult()"
    "src/providers/job.cpp: [blocking_wait] thread->wait()"
    "src/providers/job.cpp: [blocking_wait] changed.wait_for(lock, 1s)"
    "src/providers/job.cpp: [blocking_wait] latch.arrive_and_wait()"
    "src/providers/job.cpp: [blocking_wait] slots.acquire()"
    "src/providers/job.cpp: [blocking_wait] slots.try_acquire_for(1s)"
    "src/providers/job.cpp: [synchronizer] QFutureSynchronizer<void> synchronizer"
    "src/providers/job.cpp: [synchronizer] QSemaphore semaphore"
    "src/providers/job.cpp: [synchronizer] QtConcurrent::blockingMap(items, work)"
    "src/providers/job.cpp: [std_future] std::future<int> pending = std::async(work)"
    "src/providers/job.cpp: [std_thread] std::jthread worker(work)"
    "src/providers/job.cpp: [std_thread] std::condition_variable_any changed"
    "src/providers/job.cpp: [std_thread] std::counting_semaphore<4> slots{4}"
    "src/providers/job.cpp: [sync_request] request.setAttribute(QNetworkRequest::SynchronousRequestAttribute, true)"
    "src/providers/job.cpp: [blocking_connection] connect(a, &A::done, b, &B::run, Qt::BlockingQueuedConnection)"
    "src/providers/job.cpp: [exec] dialog.exec()"
    "src/cli/main.cpp: [event_loop] QCoreApplication::processEvents()"
    "src/ui/shell/main.cpp: [exec]"
    "tests/ui/test_window.cpp: [wait_for]"
    "src/providers/lexing.cpp: [event_loop] void unbalanced()"
    "src/providers/lexing.cpp: [exec] void comment_in_string()"
    "src/io/atomic_file.cpp: [sleep] ::Sleep(unmarked_ms)"
    "src/io/retry.cpp: [sleep] void sibling()"
)
foreach(reported IN LISTS expected)
    string(FIND "${output}" "${reported}" position)
    if(position EQUAL -1)
        list(APPEND failures "not reported: ${reported}")
    endif()
endforeach()
foreach(
    silent
    "comments.cpp: ["
    "src/cli/main.cpp: [exec]"
    "qt_drive.hpp: ["
    "tests/cmake/"
    "hardware_concurrency"
    "never waitForFinished"
    "never processEvents"
    "::Sleep(pause_ms)"
)
    string(FIND "${output}" "${silent}" position)
    if(NOT position EQUAL -1)
        list(APPEND failures "reported, but allowed: ${silent}")
    endif()
endforeach()
if(failures)
    list(APPEND failures "scanner output:\n${output}")
endif()

run_scanner("${CMAKE_CURRENT_LIST_DIR}/clean" result output)
if(NOT result EQUAL 0)
    list(APPEND failures "the clean tree was rejected:\n${output}")
endif()

run_scanner("${CMAKE_CURRENT_LIST_DIR}/does-not-exist" result output)
if(result EQUAL 0 OR NOT output MATCHES "does not exist")
    list(APPEND failures "a missing root was accepted")
endif()

run_scanner("${CMAKE_CURRENT_LIST_DIR}/no_tests" result output)
if(result EQUAL 0 OR NOT output MATCHES "no_tests/tests'[ \t\r\n]+does not exist")
    list(APPEND failures "a missing tests/ was accepted:\n${output}")
endif()

run_scanner("${CMAKE_CURRENT_LIST_DIR}/no_cpp" result output)
if(result EQUAL 0 OR NOT output MATCHES "No C\\+\\+ files under")
    list(APPEND failures "a tests/ without C++ files was accepted:\n${output}")
endif()

if(failures)
    list(JOIN failures "\n" report)
    message(FATAL_ERROR "${report}")
endif()
message(STATUS "Blocking-call gate: all cases behave")
