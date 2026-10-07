# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# Refuse to configure into the source tree. CMAKE_ (not PROJECT_) dirs: the
# question is whether the whole build tree is in-source.
function(mov_assure_out_of_source_builds)
    # Resolve symlinks so a linked build dir cannot sneak past the check.
    file(REAL_PATH "${CMAKE_SOURCE_DIR}" srcdir)
    file(REAL_PATH "${CMAKE_BINARY_DIR}" bindir)
    if(srcdir STREQUAL bindir)
        message(
            FATAL_ERROR
            "In-source builds are disabled. Use a preset (cmake --preset dev) or "
            "pass a separate build directory with -B. Remove CMakeCache.txt and "
            "CMakeFiles/ from the source tree first."
        )
    endif()
endfunction()

mov_assure_out_of_source_builds()
