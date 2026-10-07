# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# cmake -D "MOV_SOURCE_DIRS=<dir>;<dir>" -P CheckQtFreeSources.cmake
#
# Fails if any C++ file under the given directories includes a Qt header:
# class headers (<QString>), module and umbrella headers (<QtCore>,
# <QtGlobal>, <QtCore/qstring.h>), lower-case headers ("qglobal.h") and
# private ones (<private/qobject_p.h>). Also fails if a directory is missing
# or holds no C++ files, so a renamed layer cannot pass vacuously.
# Registered as the ctest test qt_free_sources for the Qt-free layers.

set(qt_header "(Qt[A-Za-z0-9]*(/[^>\"]*)?|Q[A-Z][A-Za-z0-9]*|q[a-z0-9_]+\\.h|private/q[a-z0-9_]+_p\\.h)")
set(qt_include_regex "^[ \t]*#[ \t]*include[ \t]*[<\"]${qt_header}[>\"]")
set(extensions
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

if(NOT MOV_SOURCE_DIRS)
    message(FATAL_ERROR "MOV_SOURCE_DIRS is empty")
endif()

set(violations "")
foreach(dir IN LISTS MOV_SOURCE_DIRS)
    if(NOT IS_DIRECTORY "${dir}")
        message(FATAL_ERROR "Source directory ${dir} does not exist")
    endif()
    set(globs "")
    foreach(extension IN LISTS extensions)
        list(APPEND globs "${dir}/*.${extension}")
    endforeach()
    file(GLOB_RECURSE sources ${globs})
    if(NOT sources)
        message(FATAL_ERROR "No C++ files under ${dir}")
    endif()
    foreach(source IN LISTS sources)
        file(STRINGS "${source}" lines REGEX "${qt_include_regex}")
        foreach(line IN LISTS lines)
            list(APPEND violations "  ${source}: ${line}")
        endforeach()
    endforeach()
endforeach()

if(violations)
    list(JOIN violations "\n" report)
    message(FATAL_ERROR "Qt headers included in Qt-free code:\n${report}")
endif()
list(JOIN MOV_SOURCE_DIRS ", " dirs)
message(STATUS "No Qt includes under ${dirs}")
