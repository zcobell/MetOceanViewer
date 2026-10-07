# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# The Qt layers start here. No Qt target exists yet: the skeleton QML/MapLibre
# app (Phase 1) is the first, so for now MOV_ENABLE_QT only proves that Qt is
# found. The Qt presets point CMAKE_PREFIX_PATH at $env{QT_ROOT_DIR}.
find_package(Qt6 6.11 REQUIRED COMPONENTS Core)
message(STATUS "Qt ${Qt6_VERSION} found at ${Qt6_DIR}")
