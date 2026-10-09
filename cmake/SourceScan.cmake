# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# Line scanner shared by the source gates that ban a pattern outside an
# allow-list (CheckBlockingCalls.cmake, CheckIgnoreSslErrors.cmake,
# CheckThirdPartyIncludes.cmake). Included by those -P scripts.
#
# mov_scan_banned(<out_var> ROOT <dir> DIRS <subdir>... [EXCLUDE <regex>] RULES <rule>...)
#
#   Scans every C++ file under <dir>/<subdir> (recursively) for each rule.
#   The caller defines, per rule:
#     <rule>_regex         a CMake regex a line's code must match to be a
#                          violation. The whole file is tested with it
#                          first, so a line anchor is written (^|\n), not ^.
#     <rule>_allowed       optional: a regex of paths, relative to <dir>,
#                          where the rule does not apply
#     <rule>_marker        optional: a text that, on the raw line (comments
#                          included), exempts that line ...
#     <rule>_marker_paths  ... in the files whose relative path matches
#   EXCLUDE is a regex of relative paths that are not scanned at all (the
#   gates' own fixture trees under tests/cmake/).
#
#   What is matched is the line's code: `//` comments and `/* ... */`
#   comments are removed, found after string and character literals are
#   masked, so "/*" or '"' in a literal cannot start or hide a comment.
#   String literals themselves are matched (a banned name in a string is
#   reported). A block comment spanning lines is matched from its second
#   line on; the project writes `///` comments.
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
    mm
)

# `count` copies of `_`.
function(_mov_underscores count out_var)
    if(count GREATER 0)
        string(REPEAT "_" ${count} filler)
    else()
        set(filler "")
    endif()
    set(${out_var} "${filler}" PARENT_SCOPE)
endfunction()

# The line with the contents of its string and character literals replaced
# by `_`, keeping every offset: escapes first (so \" ends nothing), then
# character literals ('"'), then strings.
function(_mov_mask_literals line out_var)
    string(REGEX REPLACE "\\\\." "__" masked "${line}")
    string(REGEX REPLACE "'[^']'" "'_'" masked "${masked}")
    set(offset 0)
    while(TRUE)
        string(SUBSTRING "${masked}" ${offset} -1 rest)
        string(FIND "${rest}" "\"" open)
        if(open EQUAL -1)
            break()
        endif()
        math(EXPR open "${offset} + ${open} + 1")
        string(SUBSTRING "${masked}" ${open} -1 rest)
        string(FIND "${rest}" "\"" length)
        if(length EQUAL -1)
            break()
        endif()
        _mov_underscores(${length} filler)
        string(SUBSTRING "${masked}" 0 ${open} head)
        math(EXPR close "${open} + ${length}")
        string(SUBSTRING "${masked}" ${close} -1 tail)
        set(masked "${head}${filler}${tail}")
        math(EXPR offset "${close} + 1")
    endwhile()
    set(${out_var} "${masked}" PARENT_SCOPE)
endfunction()

# The code part of one line: its comments, located on the masked line,
# blanked out of the original line.
function(_mov_code_part line out_var)
    _mov_mask_literals("${line}" masked)
    while(TRUE)
        string(FIND "${masked}" "//" line_comment)
        string(FIND "${masked}" "/*" block_comment)
        if(line_comment EQUAL -1 AND block_comment EQUAL -1)
            break()
        endif()
        if(block_comment EQUAL -1 OR (NOT line_comment EQUAL -1 AND line_comment LESS block_comment))
            string(SUBSTRING "${line}" 0 ${line_comment} line)
            break()
        endif()
        math(EXPR after "${block_comment} + 2")
        string(SUBSTRING "${masked}" ${after} -1 rest)
        string(FIND "${rest}" "*/" length)
        string(SUBSTRING "${line}" 0 ${block_comment} head)
        string(SUBSTRING "${masked}" 0 ${block_comment} masked_head)
        if(length EQUAL -1)
            set(line "${head}")
            break()
        endif()
        math(EXPR close "${after} + ${length} + 2")
        string(SUBSTRING "${line}" ${close} -1 tail)
        string(SUBSTRING "${masked}" ${close} -1 masked_tail)
        set(line "${head} ${tail}")
        set(masked "${masked_head} ${masked_tail}")
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
        set(base "${arg_ROOT}/${dir}")
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

    set(any_regex "")
    foreach(rule IN LISTS arg_RULES)
        if(NOT DEFINED ${rule}_regex)
            message(FATAL_ERROR "mov_scan_banned: ${rule}_regex is not defined")
        endif()
        list(APPEND any_regex "(${${rule}_regex})")
    endforeach()
    list(JOIN any_regex "|" any_regex)

    # Lines become list items only after `;`, `[`, `]` and `\` are replaced:
    # a `;` would split a line, and an unbalanced bracket or a trailing
    # backslash would join lines.
    string(ASCII 1 semicolon)
    string(ASCII 2 open_bracket)
    string(ASCII 3 close_bracket)
    string(ASCII 4 backslash)

    set(violations "")
    foreach(source IN LISTS sources)
        file(RELATIVE_PATH relative "${arg_ROOT}" "${source}")
        if(arg_EXCLUDE AND relative MATCHES "${arg_EXCLUDE}")
            continue()
        endif()
        file(READ "${source}" content)
        if(NOT content MATCHES "${any_regex}")
            continue()
        endif()
        string(REPLACE "\r" "" content "${content}")
        string(REPLACE "\\" "${backslash}" content "${content}")
        string(REPLACE ";" "${semicolon}" content "${content}")
        string(REPLACE "[" "${open_bracket}" content "${content}")
        string(REPLACE "]" "${close_bracket}" content "${content}")
        string(REPLACE "\n" ";" lines "${content}")
        foreach(line IN LISTS lines)
            string(REPLACE "${semicolon}" ";" line "${line}")
            string(REPLACE "${open_bracket}" "[" line "${line}")
            string(REPLACE "${close_bracket}" "]" line "${line}")
            string(REPLACE "${backslash}" "\\" line "${line}")
            if(NOT line MATCHES "${any_regex}")
                continue()
            endif()
            _mov_code_part("${line}" code)
            foreach(rule IN LISTS arg_RULES)
                if(NOT code MATCHES "${${rule}_regex}")
                    continue()
                endif()
                if(NOT "${${rule}_allowed}" STREQUAL "" AND relative MATCHES "${${rule}_allowed}")
                    continue()
                endif()
                if(NOT "${${rule}_marker}" STREQUAL "" AND relative MATCHES "${${rule}_marker_paths}")
                    string(FIND "${line}" "${${rule}_marker}" marked)
                    if(NOT marked EQUAL -1)
                        continue()
                    endif()
                endif()
                string(STRIP "${line}" shown)
                string(APPEND violations "\n  ${relative}: [${rule}] ${shown}")
            endforeach()
        endforeach()
    endforeach()
    string(STRIP "${violations}" violations)
    set(${out_var} "${violations}" PARENT_SCOPE)
endfunction()
