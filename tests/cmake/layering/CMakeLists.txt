# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# Fixture project for tests/cmake/layering/run.cmake: the real
# cmake/Layering.cmake and mov_add_module(), plus one deliberate violation
# selected by CASE. Configured only, never built.

cmake_minimum_required(VERSION 4.4)
project(layering_fixture LANGUAGES CXX)

# Stand-ins for what the real top-level CMakeLists.txt provides.
add_library(mov_options INTERFACE)
add_library(mov_warnings INTERFACE)
add_library(mov::options ALIAS mov_options)
add_library(mov::warnings ALIAS mov_warnings)
function(mov_enable_static_analysis target)
endfunction()

include("${MOV_REPO}/cmake/Layering.cmake")

# A fake Qt that looks like the real imported targets.
add_library(Qt6::Core INTERFACE IMPORTED)
set_target_properties(Qt6::Core PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "/opt/qt/include/QtCore")
# One the clean case may link (from providers up): no path to check.
add_library(Qt6::Network INTERFACE IMPORTED)

mov_add_module(core SOURCES stub.cpp)
mov_add_module(io SOURCES stub.cpp PUBLIC_LINK mov::core)
# A header-only layer is an INTERFACE library, checked the same way.
mov_add_module(fetch PUBLIC_LINK mov::io)
mov_add_module(providers SOURCES stub.cpp PUBLIC_LINK mov::fetch PRIVATE_LINK Qt6::Network)

if(CASE STREQUAL "clean")
    # No violation: configure must succeed.
elseif(CASE STREQUAL "fetch_links_qt")
    target_link_libraries(mov_fetch INTERFACE $<LINK_ONLY:Qt6::Core>)
elseif(CASE STREQUAL "io_links_fetch")
    target_link_libraries(mov_io PRIVATE mov::fetch)
elseif(CASE STREQUAL "fetch_links_providers")
    target_link_libraries(mov_fetch INTERFACE mov_providers)
elseif(CASE STREQUAL "genex_link")
    target_link_libraries(mov_core PRIVATE $<$<CONFIG:Debug>:$<LINK_ONLY:Qt6::Core>>)
elseif(CASE STREQUAL "versionless_target")
    add_library(Qt::Core ALIAS Qt6::Core)
    target_link_libraries(mov_io PRIVATE Qt::Core)
elseif(CASE STREQUAL "link_flag")
    target_link_options(mov_io PRIVATE -lQt6Core)
elseif(CASE STREQUAL "bare_library_name")
    target_link_libraries(mov_core PRIVATE Qt6Core)
elseif(CASE STREQUAL "library_path")
    target_link_libraries(mov_core PRIVATE /opt/qt/lib/libQt6Core.so.6)
elseif(CASE STREQUAL "include_dir")
    target_include_directories(mov_core PRIVATE $<TARGET_PROPERTY:Qt6::Core,INTERFACE_INCLUDE_DIRECTORIES>)
elseif(CASE STREQUAL "direct_link")
    set_property(TARGET mov_core APPEND PROPERTY INTERFACE_LINK_LIBRARIES_DIRECT Qt6::Core)
elseif(CASE STREQUAL "transitive")
    add_library(helper INTERFACE)
    target_link_libraries(helper INTERFACE Qt6::Core)
    target_link_libraries(mov_io PRIVATE helper)
elseif(CASE STREQUAL "upward_link")
    target_link_libraries(mov_core PRIVATE mov::io)
else()
    message(FATAL_ERROR "unknown CASE '${CASE}'")
endif()

mov_check_layering()
