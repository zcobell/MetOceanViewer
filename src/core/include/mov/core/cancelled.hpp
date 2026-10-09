// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

namespace mov::core {

/// The caller asked a long piece of work to stop (a stop token) and it did,
/// with no result. One alternative for every layer's error variant: io::Error
/// has it (io::Cancelled names this type), and so will the errors of the
/// tide prediction (docs/harmonics-engine.md section 2).
struct Cancelled {
  friend constexpr bool operator==(Cancelled, Cancelled) = default;
};

}  // namespace mov::core
