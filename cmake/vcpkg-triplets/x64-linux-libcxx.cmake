# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# x64-linux with C++ ports built by Clang against libc++ (clang++-libcxx, see
# tools/dev/Dockerfile), so Catch2 shares the standard library of the tests:
# libstdc++ and libc++ std::string are different types. C ports (netcdf-c, hdf5
# ...) keep the default C compiler. Used by the dev-libcxx preset.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/x64-linux-libcxx-toolchain.cmake")
