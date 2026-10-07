# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# cmake -D QMLFORMAT=<qmlformat> -D "MOV_QML_DIRS=<dir>;<dir>" -P CheckQmlFormat.cmake
#
# Fails if any .qml file under the given directories differs from what
# qmlformat (Qt's formatter, default settings) makes of it, or if there is
# none to check. Registered as the ctest test qml_format: the pre-commit job
# has no Qt, so the Qt builds check QML formatting instead. To fix:
#   qmlformat -i <files>

if(NOT QMLFORMAT OR NOT MOV_QML_DIRS)
    message(FATAL_ERROR "QMLFORMAT and MOV_QML_DIRS must be set")
endif()

set(sources "")
foreach(dir IN LISTS MOV_QML_DIRS)
    file(GLOB_RECURSE found "${dir}/*.qml")
    list(APPEND sources ${found})
endforeach()
if(NOT sources)
    message(FATAL_ERROR "No .qml files under ${MOV_QML_DIRS}")
endif()

set(unformatted "")
foreach(source IN LISTS sources)
    execute_process(
        COMMAND "${QMLFORMAT}" "${source}"
        OUTPUT_VARIABLE formatted
        ERROR_VARIABLE errors
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "qmlformat failed on ${source}:\n${errors}")
    endif()
    file(READ "${source}" original)
    if(NOT original STREQUAL formatted)
        list(APPEND unformatted "${source}")
    endif()
endforeach()

if(unformatted)
    list(JOIN unformatted "\n  " report)
    message(FATAL_ERROR "Not qmlformat-formatted (fix with qmlformat -i):\n  ${report}")
endif()
list(LENGTH sources count)
message(STATUS "${count} QML files formatted")
