# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# Chainloaded by x64-linux-libcxx.cmake: C++ through clang++-libcxx with
# -stdlib=libc++. No -fexperimental-library, as with Apple's toolchain.
set(CMAKE_CXX_COMPILER clang++-libcxx)
string(APPEND CMAKE_CXX_FLAGS_INIT " -stdlib=libc++")
