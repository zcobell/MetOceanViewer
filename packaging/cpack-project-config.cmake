# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# CPACK_PROJECT_CONFIG_FILE: included by cpack once per generator, with
# CPACK_GENERATOR set to that generator.

# The Windows installer and the portable zip share a base name; the zip says
# what it is.
if(CPACK_GENERATOR STREQUAL "ZIP")
    string(APPEND CPACK_PACKAGE_FILE_NAME "-portable")
endif()
