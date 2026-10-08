# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# x64-windows cross-built from Linux with clang-cl against the xwin-provided
# MSVC STL/CRT and Windows SDK (cmake/toolchains/clang-cl-xwin.cmake; the msvc
# dev image, tools/dev/msvc). Static libraries and the dynamic CRT (/MD): the
# test executables then need no DLLs from the ports. Used by the dev-msvc-xwin
# preset.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
# VCPKG_CMAKE_SYSTEM_NAME stays unset: that is how vcpkg knows the target is
# Windows (VCPKG_TARGET_IS_WINDOWS); "Windows" there would make it a Unix.
# Release only: xwin does not install the debug CRT (msvcrtd.lib), and the
# preset builds RelWithDebInfo as the windows-2022 job does.
set(VCPKG_BUILD_TYPE release)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../toolchains/clang-cl-xwin.cmake")
