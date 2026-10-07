// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The mechanism of an atomic file replacement (core-design.md C16, section
// 4.4), shared by the text writer here and the netCDF writer of WP6:
//
//   0. check   an existing target must be writable (check_target_replaceable)
//   1. create  a unique temporary file next to the target, exclusively
//   2. body    the caller writes it
//   3. close   the caller closes it
//   4. fsync_file  the temporary file reaches the disk
//   5. rename  the temporary file replaces the target in one step; before it,
//              the target's permission bits are copied onto the temporary file
//   6. fsync_dir   the rename reaches the disk (POSIX)
//
// The target is either what it was or the complete new file. Stages 1 to 3
// belong to the writer (a stdio stream here, nc_create/nc_close for netCDF);
// stages 4 to 6 are commit_temp. Every stage can be made to fail by a
// FaultInjector, which is how the tests cover the failure paths.

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <ostream>
#include <system_error>
#include <utility>

#include "mov/io/error.hpp"

namespace mov::io::detail {

enum class AtomicStage : std::uint8_t {
  create,
  body,
  close,
  fsync_file,
  rename,
  fsync_dir,
};

/// Test hooks. `fail_at` makes that stage fail the way a failing system call
/// would, with the error `code`, without calling it; a failing `body` or
/// `close` has run the real one first. `temp_suffix` replaces the random part
/// of the temporary file's name.
struct FaultInjector {
  std::optional<AtomicStage> fail_at{};
  std::error_code code{std::make_error_code(std::errc::io_error)};
  std::optional<std::uint64_t> temp_suffix{};

  [[nodiscard]] bool fails(AtomicStage stage) const noexcept {
    return fail_at == stage;
  }
};

/// `<directory of target>/.mov-<16 hex digits>.tmp`: a name of fixed length in
/// the target's directory (a long target name cannot make it too long). The
/// digits are random (64 bits from std::random_device) unless `forced_suffix`
/// is given.
[[nodiscard]] std::filesystem::path temp_path_for(
    const std::filesystem::path& target,
    std::optional<std::uint64_t> forced_suffix);

/// Stage 0. An existing regular file that the owner may not write is
/// `permission_denied` (the replacement would silently override a read-only
/// mark); a missing target, or one that is not a regular file, passes.
[[nodiscard]] std::expected<void, FileError> check_target_replaceable(
    const std::filesystem::path& target);

/// Stages 4 to 6, preceded by copying the permission bits of an existing
/// regular target onto `temp`. On an error before the rename `temp` still
/// exists and the caller's TempFileGuard removes it.
///
/// A directory fsync that the file system does not support (EINVAL,
/// ENOTSUP, EOPNOTSUPP) is not an error: there is nothing more to do. Any
/// other failure of `fsync_dir` comes after the rename: the target holds the
/// new content and `temp` is gone, but the rename may not survive a crash.
[[nodiscard]] std::expected<void, FileError> commit_temp(
    const std::filesystem::path& target, const std::filesystem::path& temp,
    const FaultInjector& fault);

/// Removes the temporary file when it goes out of scope, unless released.
/// Errors are ignored: the primary error is the one worth reporting. Declare
/// it before the stream that writes the file, so the stream is closed first.
class TempFileGuard {
 public:
  explicit TempFileGuard(std::filesystem::path temp) : temp_{std::move(temp)} {}
  TempFileGuard(const TempFileGuard&) = delete;
  TempFileGuard& operator=(const TempFileGuard&) = delete;
  TempFileGuard(TempFileGuard&&) = delete;
  TempFileGuard& operator=(TempFileGuard&&) = delete;
  ~TempFileGuard() {
    if (armed_) {
      std::error_code ignored;
      std::filesystem::remove(temp_, ignored);
    }
  }

  /// The temporary file is the target now (or must stay): do not remove it.
  void release() noexcept { armed_ = false; }

 private:
  std::filesystem::path temp_;
  bool armed_{true};
};

/// What the body of a text write returns.
using TextBodyResult = std::expected<void, Error>;

/// Writes `target` atomically through a binary stream. A body that returns an
/// error (or throws) leaves the target as it was and removes the temporary
/// file; the error is returned as it is. A stream in a failed state after the
/// body is a `FileOp::write` error.
[[nodiscard]] std::expected<void, Error> write_text_atomic(
    const std::filesystem::path& target,
    const std::function<TextBodyResult(std::ostream&)>& body,
    const FaultInjector& fault = {});

}  // namespace mov::io::detail
