# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# Line scanner shared by the source gates that ban a pattern outside an
# allow-list (CheckBlockingCalls.cmake, CheckIgnoreSslErrors.cmake,
# CheckThirdPartyIncludes.cmake). Included by those -P scripts.
#
# mov_scan_banned(<out_var> ROOT <dir> DIRS <subdir>... [EXCLUDE <regex>] RULES <rule>...)
#
#   Scans every C++ file under <dir>/<subdir> (recursively; `.` is <dir>
#   itself) for each rule.
#   The caller defines, per rule, the variables
#     <rule>_regex    a CMake regex a line must match to be a violation
#     <rule>_allowed  a regex of paths, relative to <dir>, where the rule
#                     does not apply (empty: nowhere)
#   EXCLUDE is a regex of paths, relative to <dir>, that are not scanned at
#   all (the gates' own fixture trees under tests/cmake/).
#   Text after `//` (outside a string literal) and inside a one-line
#   `/* ... */` is not code and is not matched, so a comment may name a
#   banned call. Multi-line block comments are matched; the project writes
#   `///` comments.
#   <out_var> receives a text with one line per violation,
#   "  <path>: [<rule>] <line>", or an empty string.
#   Fails if <dir>/<subdir> is missing or holds no C++ file, so a renamed
#   directory cannot pass vacuously.

set(_mov_scan_extensions
    cpp
    cc
    cxx
    c++
    hpp
    hh
    hxx
    h
    ipp
    inl
    tpp
    ixx
    cppm
)

# The code part of one line: without a `//` comment that starts outside a
# string literal, and without one-line `/* ... */` comments. Escaped
# characters are blanked first so `\"` does not end a string.
function(_mov_code_part line out_var)
    string(REGEX REPLACE "/\\*[^*]*\\*+([^/*][^*]*\\*+)*/" " " line "${line}")
    string(REGEX REPLACE "\\\\." "__" masked "${line}")
    set(offset 0)
    while(TRUE)
        string(SUBSTRING "${masked}" ${offset} -1 rest)
        string(FIND "${rest}" "//" found)
        if(found EQUAL -1)
            break()
        endif()
        math(EXPR position "${offset} + ${found}")
        string(SUBSTRING "${masked}" 0 ${position} prefix)
        string(REGEX MATCHALL "\"" quotes "${prefix}")
        list(LENGTH quotes count)
        math(EXPR odd "${count} % 2")
        if(odd EQUAL 0)
            string(SUBSTRING "${line}" 0 ${position} line)
            break()
        endif()
        math(EXPR offset "${position} + 2")
    endwhile()
    set(${out_var} "${line}" PARENT_SCOPE)
endfunction()

function(mov_scan_banned out_var)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "ROOT;EXCLUDE" "DIRS;RULES")
    if(NOT arg_ROOT OR NOT IS_DIRECTORY "${arg_ROOT}")
        message(FATAL_ERROR "Source directory '${arg_ROOT}' does not exist")
    endif()
    if(NOT arg_DIRS OR NOT arg_RULES)
        message(FATAL_ERROR "mov_scan_banned: DIRS and RULES are required")
    endif()

    set(sources "")
    foreach(dir IN LISTS arg_DIRS)
        cmake_path(APPEND arg_ROOT "${dir}" OUTPUT_VARIABLE base)
        cmake_path(NORMAL_PATH base)
        if(NOT IS_DIRECTORY "${base}")
            message(FATAL_ERROR "Source directory '${base}' does not exist")
        endif()
        set(globs "")
        foreach(extension IN LISTS _mov_scan_extensions)
            list(APPEND globs "${base}/*.${extension}")
        endforeach()
        file(GLOB_RECURSE found ${globs})
        if(NOT found)
            message(FATAL_ERROR "No C++ files under ${base}")
        endif()
        list(APPEND sources ${found})
    endforeach()

    # One prefilter per file: the union of the rules' patterns.
    set(any_regex "")
    foreach(rule IN LISTS arg_RULES)
        if(NOT DEFINED ${rule}_regex)
            message(FATAL_ERROR "mov_scan_banned: ${rule}_regex is not defined")
        endif()
        list(APPEND any_regex "(${${rule}_regex})")
    endforeach()
    list(JOIN any_regex "|" any_regex)

    set(violations "")
    foreach(source IN LISTS sources)
        file(RELATIVE_PATH relative "${arg_ROOT}" "${source}")
        if(arg_EXCLUDE AND relative MATCHES "${arg_EXCLUDE}")
            continue()
        endif()
        file(STRINGS "${source}" lines REGEX "${any_regex}")
        foreach(line IN LISTS lines)
            _mov_code_part("${line}" code)
            foreach(rule IN LISTS arg_RULES)
                if(NOT code MATCHES "${${rule}_regex}")
                    continue()
                endif()
                if(NOT "${${rule}_allowed}" STREQUAL "" AND relative MATCHES "${${rule}_allowed}")
                    continue()
                endif()
                string(STRIP "${line}" shown)
                string(APPEND violations "\n  ${relative}: [${rule}] ${shown}")
            endforeach()
        endforeach()
    endforeach()
    string(STRIP "${violations}" violations)
    set(${out_var} "${violations}" PARENT_SCOPE)
endfunction()
