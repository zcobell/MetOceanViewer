# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# cmake -D "MOV_REPO=<root>" -P CheckIgnoreSslErrors.cmake
#
# Fails if a C++ file under <root>/src or <root>/tests turns off TLS
# certificate checks: ignoreSslErrors, or QSslSocket::VerifyNone
# (docs/providers-design.md §2.2, §6.2). The tests that need a server use
# plain HTTP on loopback. No exception. The gates' own fixtures (tests/cmake/)
# are not scanned. Registered as the ctest test no_ignore_ssl_errors.

cmake_minimum_required(VERSION 4.4)

include("${CMAKE_CURRENT_LIST_DIR}/SourceScan.cmake")

if(NOT MOV_REPO)
    message(FATAL_ERROR "MOV_REPO is not set")
endif()

set(ignore_ssl_errors_regex "ignoreSslErrors")
set(ignore_ssl_errors_allowed "")
set(verify_none_regex "VerifyNone")
set(verify_none_allowed "")

mov_scan_banned(
    violations
    ROOT "${MOV_REPO}"
    DIRS src tests
    EXCLUDE "^tests/cmake/"
    RULES ignore_ssl_errors verify_none
)

if(violations)
    message(FATAL_ERROR "TLS certificate checks turned off:\n  ${violations}")
endif()
message(STATUS "No TLS certificate check is turned off under ${MOV_REPO}/src and ${MOV_REPO}/tests")
