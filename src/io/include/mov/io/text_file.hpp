// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <concepts>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

#include "mov/io/detail/atomic_file.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"

namespace mov::io {

/// The whole file as bytes, exactly as stored: a byte order mark, CR bytes
/// and NULs are kept (LineCursor handles the first two, and the parsers the
/// rest). The path must name a regular file; a missing file, a directory or
/// any other kind of file is a FileError, never an empty success (B3). The
/// file is opened first (without blocking, so a FIFO cannot hang the reader)
/// and everything after that is asked of the open file, not of the name: what
/// is checked is what is read. The size is checked against
/// `limits.max_text_bytes` before anything is allocated (`errc::file_too_large`
/// on `FileOp::size`). A file that grows while it is read is cut at the size
/// first seen; one that shrinks gives what was there.
///
/// Blocking I/O: call it from a worker, not the GUI thread.
[[nodiscard]] std::expected<std::string, FileError> read_text_file(
    const std::filesystem::path& path, const ReadLimits& limits);

/// The start of a file: its first bytes, and whether they are all of it.
struct FilePrefix {
  std::string bytes;
  /// The file has no more bytes than `bytes` (it is not cut).
  bool whole_file;
  friend bool operator==(const FilePrefix&, const FilePrefix&) = default;
};

/// The first `max_bytes` bytes of the file (all of it if it is shorter), for a
/// reader that only sniffs what kind of file it has. Opened and checked like
/// read_text_file (a missing file, a directory or any other kind of file is a
/// FileError), but whatever the size of the file: no `max_text_bytes` applies,
/// because no more than `max_bytes` are read.
[[nodiscard]] std::expected<FilePrefix, FileError> read_file_prefix(
    const std::filesystem::path& path, std::size_t max_bytes);

namespace detail {

template <class R>
inline constexpr bool is_expected_void_v = false;
template <class E>
inline constexpr bool is_expected_void_v<std::expected<void, E>> = true;

template <class R>
struct ExpectedVoidError {};
template <class E>
struct ExpectedVoidError<std::expected<void, E>> {
  using type = E;
};

}  // namespace detail

/// The body of write_file_atomic: it writes to the stream and reports success
/// or its own failure as `std::expected<void, E>`, with E convertible to
/// `io::Error` (a `FileError`, a `FormatError`, or `Error` itself). A body that
/// returns `void`, or a `bool` or an `int`, has no way to say it failed and
/// is not accepted.
template <class Body>
concept AtomicTextBody =
    std::invocable<Body, std::ostream&> and
    detail::is_expected_void_v<
        std::remove_cvref_t<std::invoke_result_t<Body, std::ostream&>>> and
    std::convertible_to<typename detail::ExpectedVoidError<std::remove_cvref_t<
                            std::invoke_result_t<Body, std::ostream&>>>::type,
                        Error>;

/// Replaces `path` with what `body` writes to the stream, atomically: the
/// target is the old file or the complete new one, never a part, and no
/// temporary file is left behind on failure. The sequence (check the target,
/// create a unique temporary beside it exclusively, write, close, fsync it,
/// copy the permissions, rename it over the target, fsync the directory) is in
/// detail/atomic_file.hpp.
///
/// If `body` returns an error, that error is returned unchanged and the
/// target is untouched. The stream is binary; a stream in a failed state after
/// the body is a `FileOp::write` error with the cause from the C library
/// (`ENOSPC`, say). `body` may also throw: the temporary file is removed and
/// the exception propagates. An existing target without owner write permission
/// is `permission_denied`; otherwise the new file has the permission bits of
/// the old one. A symbolic link at `path` is replaced, not followed.
template <AtomicTextBody Body>
[[nodiscard]] std::expected<void, Error> write_file_atomic(
    const std::filesystem::path& path, Body&& body) {
  return detail::write_text_atomic(
      path, [&body](std::ostream& out) -> detail::TextBodyResult {
        return std::invoke(std::forward<Body>(body), out)
            .transform_error(lift<Error>);
      });
}

}  // namespace mov::io
