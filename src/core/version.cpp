// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/version.hpp"

#include "mov/core/build_info.hpp"

namespace mov::core {

std::string_view version() noexcept { return build_info::version; }

VersionNumber version_number() noexcept {
  return VersionNumber{.major_version = build_info::version_major,
                       .minor_version = build_info::version_minor,
                       .patch_version = build_info::version_patch};
}

}  // namespace mov::core
