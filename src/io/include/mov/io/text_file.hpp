// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <concepts>
#include <expected>
#include <filesystem>
#include <functional>
#include <ostream>
#include <string>

#include "mov/io/detail/atomic_file.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"

namespace mov::io {

/// The whole file as bytes, exactly as stored: a byte order mark, CR bytes
/// and NULs are kept (LineCursor handles the first two, and the parsers the
/// rest). The path must name a regular file; a missing file, a directory or
/// any other kind of file is a FileError, never an empty success (B3). The
/// size is read from the file system and checked against
/// `limits.max_text_bytes` before anything is allocated (`errc::file_too_large`
/// on `FileOp::size`). A file that grows while it is read is cut at the size
/// first seen.
///
/// Blocking I/O: call it from a worker, not the GUI thread.
[[nodiscard]] std::expected<std::string, FileError> read_text_file(
    const std::filesystem::path& path, const ReadLimits& limits);

/// Replaces `path` with what `body` writes to the stream, atomically: the
/// target is the old file or the complete new one, never a part, and no
/// temporary file is left behind on failure. The sequence (create a unique
/// temporary beside the target exclusively, write, close, fsync it, rename it
/// over the target, fsync the directory) is in detail/atomic_file.hpp.
///
/// The stream is binary. A failed stream (a write error, or `body` setting
/// failbit) fails the write. `body` may also throw: the temporary file is
/// removed and the exception propagates. The new file gets the default
/// permissions of a new file, not those of the file it replaces; a symbolic
/// link at `path` is replaced, not followed.
template <std::invocable<std::ostream&> Body>
[[nodiscard]] std::expected<void, FileError> write_file_atomic(
    const std::filesystem::path& path, Body&& body) {
  return detail::write_text_atomic(
      path, [&body](std::ostream& out) { std::invoke(body, out); });
}

}  // namespace mov::io
