# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# One directory per layer; cmake/Layering.cmake declares their order and
# mov_add_module() builds each one.
add_subdirectory(core)
add_subdirectory(io)
add_subdirectory(fetch)

if(MOV_ENABLE_QT)
    add_subdirectory(providers)
    add_subdirectory(ui)
endif()
