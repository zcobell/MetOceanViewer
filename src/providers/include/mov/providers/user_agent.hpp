// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <QString>

namespace mov::providers {

/// The User-Agent of every request, "MetOceanViewer/<version>
/// (+https://github.com/zcobell/MetOceanViewer)": the version and a contact
/// let a provider tell the project's traffic apart and reach its maintainer
/// (docs/providers-design.md §6.1).
[[nodiscard]] QString default_user_agent();

}  // namespace mov::providers
