// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// Parts and merging (docs/providers-design.md §5.4) live in io
// (mov/io/part.hpp), where the parsers build them; fetch names them here and
// lifts a merge error into its own Error.

#include "mov/fetch/error.hpp"
#include "mov/io/part.hpp"

namespace mov::fetch {

using io::clip;
using io::ColumnKey;
using io::combine;
using io::Combined;
using io::DropTally;
using io::DuplicateColumn;
using io::fold;
using io::MergeError;
using io::Part;

/// A merge error as a fetch error.
[[nodiscard]] Error lift_merge(MergeError e);

}  // namespace mov::fetch
