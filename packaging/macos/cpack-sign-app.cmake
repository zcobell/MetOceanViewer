# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# CPACK_PRE_BUILD_SCRIPTS (DragNDrop): signs, and when the credentials are
# set notarizes and staples, the staged MetOceanViewer.app before CPack puts
# it into the disk image. sign.sh reads the APPLE_* environment; without it
# the app gets an ad-hoc signature.

file(GLOB apps LIST_DIRECTORIES true "${CPACK_TEMPORARY_DIRECTORY}/*.app" "${CPACK_TEMPORARY_DIRECTORY}/*/*.app")
list(LENGTH apps count)
if(NOT count EQUAL 1)
    message(FATAL_ERROR "Expected one .app under ${CPACK_TEMPORARY_DIRECTORY}, found: ${apps}")
endif()
execute_process(
    COMMAND
        "${CPACK_MOV_PACKAGING_DIR}/macos/sign.sh" app "${apps}" "${CPACK_MOV_PACKAGING_DIR}/macos/entitlements.plist"
    COMMAND_ERROR_IS_FATAL ANY
)
