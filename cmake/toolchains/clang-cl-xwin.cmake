# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# Cross-compile for x86-64 Windows from Linux with clang-cl (MSVC command line,
# Clang front end) against the MSVC STL/CRT and the Windows SDK that xwin
# downloaded (tools/dev/msvc/Dockerfile, /opt/xwin). Used by the dev-msvc-xwin
# preset and, through x64-windows-xwin.cmake, by the vcpkg ports. This is a
# local cross-check, not the release toolchain: tools/dev/README.md lists what
# it can and cannot catch.
#
#   XWIN_ROOT  the xwin splat directory (crt/, sdk/); default /opt/xwin.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if(NOT DEFINED XWIN_ROOT)
    if(DEFINED ENV{XWIN_ROOT})
        set(XWIN_ROOT "$ENV{XWIN_ROOT}")
    else()
        set(XWIN_ROOT /opt/xwin)
    endif()
endif()
if(NOT IS_DIRECTORY "${XWIN_ROOT}/crt/include" OR NOT IS_DIRECTORY "${XWIN_ROOT}/sdk/include/ucrt")
    message(
        FATAL_ERROR
        "XWIN_ROOT=${XWIN_ROOT} has no MSVC CRT / Windows SDK; use the msvc dev image (MOV_DEV_IMAGE=msvc tools/dev/run.sh ...)"
    )
endif()

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_ASM_MASM_COMPILER llvm-ml)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_LINKER lld-link)
set(CMAKE_AR llvm-lib)
set(CMAKE_MT llvm-mt)
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

# The MSVC STL, UCRT and Windows SDK headers are system headers (/imsvc), so
# their warnings stay out, as with cl's external includes. 19.44 is the _MSC_VER
# of the 14.44 toolset (VS 2022 17.14) the headers come from.
set(_xwin_include_dirs
    "${XWIN_ROOT}/crt/include"
    "${XWIN_ROOT}/sdk/include/ucrt"
    "${XWIN_ROOT}/sdk/include/um"
    "${XWIN_ROOT}/sdk/include/shared"
)
# -Wno-unused-command-line-argument: clang-cl ignores cl switches that are moot
# for it (/Zc:preprocessor: its preprocessor is conforming), and /WX would turn
# that note into an error.
set(_xwin_flags "--target=x86_64-pc-windows-msvc -fms-compatibility-version=19.44 -Wno-unused-command-line-argument")
set(_xwin_rc_flags "")
foreach(_dir IN LISTS _xwin_include_dirs)
    string(APPEND _xwin_flags " /imsvc ${_dir}")
    string(APPEND _xwin_rc_flags " -I ${_dir}")
endforeach()
set(CMAKE_C_FLAGS_INIT "${_xwin_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_xwin_flags}")
set(CMAKE_RC_FLAGS_INIT "${_xwin_rc_flags}")

set(_xwin_link_flags "")
foreach(_dir "${XWIN_ROOT}/crt/lib/x86_64" "${XWIN_ROOT}/sdk/lib/um/x86_64" "${XWIN_ROOT}/sdk/lib/ucrt/x86_64")
    string(APPEND _xwin_link_flags " /libpath:${_dir}")
endforeach()
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_xwin_link_flags}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_xwin_link_flags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_xwin_link_flags}")

# /MD, as the windows-2022 job (CMake's default MSVC runtime). Configure-time
# checks (try_compile) build Release: ports with an old cmake_minimum_required
# ignore CMAKE_MSVC_RUNTIME_LIBRARY and would link the debug CRT (/MDd), which
# the xwin image does not have.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)

# InstallRequiredSystemLibraries (HDF5's install rules) looks for a Visual
# Studio installation, which only a Windows host can answer: use an empty
# stand-in.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/xwin-modules")

# Programs come from the host; libraries, headers and packages only from the
# target roots.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
