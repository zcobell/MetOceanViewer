# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# CPACK_POST_BUILD_SCRIPTS (DragNDrop): signs, notarizes and staples each disk
# image CPack made, when the APPLE_* environment provides the credentials
# (sign.sh); otherwise the image stays unsigned.

foreach(package IN LISTS CPACK_PACKAGE_FILES)
    if(package MATCHES "\\.dmg$")
        execute_process(COMMAND "${CPACK_MOV_PACKAGING_DIR}/macos/sign.sh" dmg "${package}" COMMAND_ERROR_IS_FATAL ANY)
    endif()
endforeach()
