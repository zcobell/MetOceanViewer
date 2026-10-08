# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# Stand-in for CMake's module of the same name, found first through
# CMAKE_MODULE_PATH (clang-cl-xwin.cmake). The real one looks for a Visual
# Studio installation with cmake_host_system_information(VS_<n>_DIR), which
# only a Windows host answers, and fails the configure of HDF5 on Linux. There
# is no Visual C++ redistributable to install in a cross-check.
