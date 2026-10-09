# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# cmake -D "MOV_REPO=<root>" -P CheckCoreDetailFence.cmake
#
# Fails if a C++ file under <root>/src or <root>/tests, other than core's own
# (src/core, tests/core) and the guards' fixtures (tests/cmake), names
# mov::core::detail or includes a header from mov/core/detail/. Those names
# are not API (src/core/include/mov/core/detail/core_key.hpp): a helper
# another layer needs is made public instead. Registered as the ctest test
# core_detail_fence.

set(fence_regex "core::detail|mov/core/detail/")
set(extensions
    cpp
    cc
    cxx
    hpp
    hh
    hxx
    h
    ipp
    inl
    tpp
)

if(NOT MOV_REPO OR NOT IS_DIRECTORY "${MOV_REPO}/src")
    message(FATAL_ERROR "Source directory '${MOV_REPO}/src' does not exist")
endif()

set(globs "")
foreach(root IN ITEMS src tests)
    foreach(extension IN LISTS extensions)
        list(APPEND globs "${MOV_REPO}/${root}/*.${extension}")
    endforeach()
endforeach()
file(GLOB_RECURSE sources ${globs})

set(exempt_regex "^${MOV_REPO}/(src/core|tests/core|tests/cmake)/")
set(violations "")
foreach(source IN LISTS sources)
    if(source MATCHES "${exempt_regex}")
        continue()
    endif()
    file(STRINGS "${source}" lines REGEX "${fence_regex}")
    foreach(line IN LISTS lines)
        list(APPEND violations "  ${source}: ${line}")
    endforeach()
endforeach()

if(violations)
    list(JOIN violations "\n" report)
    message(FATAL_ERROR "mov::core::detail used outside the core:\n${report}")
endif()
message(STATUS "No file outside the core uses mov::core::detail")
