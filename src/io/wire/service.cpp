// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/wire/service.hpp"

#include <string>
#include <string_view>

#include "mov/core/ascii.hpp"
#include "mov/io/error.hpp"
#include "mov/io/wire/origin.hpp"

namespace mov::io::wire {

ServiceError ServiceError::make(Origin origin, ServiceErrc code,
                                std::string_view text) {
  return ServiceError{origin, code,
                      std::string{detail::truncate_utf8(core::ascii::trim(text),
                                                        max_text_bytes)}};
}

}  // namespace mov::io::wire
