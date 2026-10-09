// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/detail/atomic_file.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <random>
#include <streambuf>
#include <system_error>
#include <utility>

#include "file_handle.hpp"

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace mov::io::detail {

namespace {

FileError file_error(FileOp op, const std::filesystem::path& target,
                     std::error_code ec) {
  return FileError{.op = op, .path = target, .ec = ec};
}

// The error just reported by the C library, or `fallback` when it set none
// (callers clear errno before the call, so a stale value cannot leak in).
std::error_code errno_code(std::errc fallback = std::errc::io_error) noexcept {
  return errno != 0 ? std::error_code{errno, std::generic_category()}
                    : std::make_error_code(fallback);
}

// ---- the platform ----------------------------------------------------------

#if defined(_WIN32)

std::error_code last_error_code() noexcept {
  return std::error_code{static_cast<int>(::GetLastError()),
                         std::system_category()};
}

// FlushFileBuffers on the file.
std::error_code fsync_file_path(const std::filesystem::path& file) noexcept {
  const FileHandle handle{
      ::CreateFileW(file.c_str(), GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
  if (not handle.is_open()) {
    return last_error_code();
  }
  return ::FlushFileBuffers(handle.get()) != 0 ? std::error_code{}
                                               : last_error_code();
}

// There is no directory step on Windows: a directory cannot be opened for
// flushing the way a POSIX directory can.
std::error_code fsync_directory_path(const std::filesystem::path&) noexcept {
  return {};
}

// MOVEFILE_REPLACE_EXISTING replaces the target; MOVEFILE_WRITE_THROUGH makes
// the call return only after the move has been flushed to disk, which is the
// durability the POSIX directory fsync gives. A virus scanner or an indexer
// that has the target or the temporary file open makes the move fail
// transiently (ACCESS_DENIED, SHARING_VIOLATION), so it is retried briefly.
std::error_code replace_file(const std::filesystem::path& from,
                             const std::filesystem::path& to) noexcept {
  constexpr int attempts = 5;
  constexpr DWORD pause_ms = 20;
  std::error_code ec;
  for (int attempt = 0; attempt < attempts; ++attempt) {
    if (::MoveFileExW(from.c_str(), to.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) !=
        0) {
      return {};
    }
    const DWORD error = ::GetLastError();
    ec = last_error_code();
    if (error != ERROR_ACCESS_DENIED and error != ERROR_SHARING_VIOLATION) {
      break;
    }
    ::Sleep(pause_ms);  // gate: bounded-retry
  }
  return ec;
}

#else

// macOS fsync does not flush the drive's cache; F_FULLFSYNC does. Only a file
// system that does not know the request (ENOTSUP, EINVAL, ENOTTY) falls back
// to fsync; any other failure of F_FULLFSYNC is a failure.
#if defined(F_FULLFSYNC)
bool full_fsync_unsupported(std::error_code ec) noexcept {
  return ec == std::errc::not_supported or ec == std::errc::invalid_argument or
         ec == std::errc::inappropriate_io_control_operation;
}
#endif

std::error_code fsync_path(const std::filesystem::path& p,
                           int extra_flags) noexcept {
  errno = 0;
  const FileHandle fd{::open(p.c_str(), O_RDONLY | O_CLOEXEC | extra_flags)};
  if (not fd.is_open()) {
    return errno_code();
  }
#if defined(F_FULLFSYNC)
  if (::fcntl(fd.get(), F_FULLFSYNC) == 0) {
    return {};
  }
  if (const std::error_code ec = errno_code(); not full_fsync_unsupported(ec)) {
    return ec;
  }
#endif
  errno = 0;
  return ::fsync(fd.get()) == 0 ? std::error_code{} : errno_code();
}

std::error_code fsync_file_path(const std::filesystem::path& file) noexcept {
  return fsync_path(file, 0);
}

std::error_code fsync_directory_path(
    const std::filesystem::path& dir) noexcept {
  return fsync_path(dir, O_DIRECTORY);
}

std::error_code replace_file(const std::filesystem::path& from,
                             const std::filesystem::path& to) noexcept {
  std::error_code ec;
  std::filesystem::rename(from, to, ec);
  return ec;
}

#endif

// ---- stages ----------------------------------------------------------------

// What to flush after a rename: the directory that holds the target.
std::filesystem::path containing_directory(
    const std::filesystem::path& target) {
  const std::filesystem::path parent = target.parent_path();
  return parent.empty() ? std::filesystem::path{"."} : parent;
}

// A directory fsync the file system does not implement leaves nothing to do.
bool directory_fsync_unsupported(std::error_code ec) noexcept {
  return ec == std::errc::invalid_argument or ec == std::errc::not_supported or
         ec == std::errc::operation_not_supported;
}

// Runs `operation` for `stage` unless the injector fails it first.
template <class Operation>
std::expected<void, FileError> run_stage(AtomicStage stage, FileOp op,
                                         const std::filesystem::path& target,
                                         const FaultInjector& fault,
                                         const Operation& operation) {
  const std::error_code ec = fault.fails(stage) ? fault.code : operation();
  const bool exempt =
      stage == AtomicStage::fsync_dir and directory_fsync_unsupported(ec);
  if (ec and not exempt) {
    return std::unexpected{file_error(op, target, ec)};
  }
  return {};
}

// The new file keeps the permission bits of the regular file it replaces.
std::expected<void, FileError> copy_permissions(
    const std::filesystem::path& target, const std::filesystem::path& temp) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::file_status existing = fs::status(target, ec);
  if (ec or existing.type() != fs::file_type::regular) {
    return {};  // nothing to preserve
  }
  fs::permissions(
      temp,
      existing.permissions() &
          (fs::perms::owner_all | fs::perms::group_all | fs::perms::others_all),
      fs::perm_options::replace, ec);
  if (ec) {
    return std::unexpected{file_error(FileOp::permissions, target, ec)};
  }
  return {};
}

// ---- the text stream -------------------------------------------------------

struct FileCloser {
  void operator()(std::FILE* file) const noexcept { std::fclose(file); }
};
using FilePtr = std::unique_ptr<std::FILE, FileCloser>;

// Opens `temp` for writing, failing if it exists (mode "x": O_EXCL on POSIX,
// CREATE_NEW on Windows), in one step: there is no moment at which the name
// is created but not ours.
std::expected<FilePtr, std::error_code> open_exclusive(
    const std::filesystem::path& temp) {
  errno = 0;
#if defined(_WIN32)
  // _wfopen_s (not the deprecated _wfopen) returns its error directly.
  std::FILE* raw = nullptr;
  if (const errno_t err = ::_wfopen_s(&raw, temp.c_str(), L"wbx"); err != 0) {
    return std::unexpected{std::error_code{err, std::generic_category()}};
  }
#else
  std::FILE* const raw = std::fopen(temp.c_str(), "wbx");
#endif
  if (raw == nullptr) {
    return std::unexpected{errno_code()};
  }
  return FilePtr{raw};
}

// Closes the file now, to see its error (the last buffered bytes reach the
// file system here, so ENOSPC often appears only now).
std::error_code close_file(FilePtr& file) noexcept {
  errno = 0;
  return std::fclose(file.release()) == 0 ? std::error_code{} : errno_code();
}

// A std::ostream over a stdio file. It has no buffer of its own (stdio
// buffers), and keeps the errno of the first failure so the writer can
// report the real cause.
class StdioStreambuf final : public std::streambuf {
 public:
  explicit StdioStreambuf(std::FILE* file) noexcept : file_{file} {}

  [[nodiscard]] std::error_code error() const noexcept { return error_; }

 protected:
  int_type overflow(int_type ch) override {
    if (traits_type::eq_int_type(ch, traits_type::eof())) {
      return traits_type::not_eof(ch);
    }
    const char c = traits_type::to_char_type(ch);
    return write(&c, 1) == 1 ? ch : traits_type::eof();
  }

  std::streamsize xsputn(const char* data, std::streamsize count) override {
    return write(data, count);
  }

  int sync() override {
    errno = 0;
    if (std::fflush(file_) == 0) {
      return 0;
    }
    remember(errno_code());
    return -1;
  }

 private:
  std::streamsize write(const char* data, std::streamsize count) {
    errno = 0;
    const auto wanted = static_cast<std::size_t>(count);
    const std::size_t written = std::fwrite(data, 1, wanted, file_);
    if (written != wanted) {
      remember(errno_code());
    }
    return static_cast<std::streamsize>(written);
  }

  void remember(std::error_code ec) noexcept {
    if (not error_) {
      error_ = ec;
    }
  }

  std::FILE* file_;
  std::error_code error_;
};

// Stage 2: the body writes; a failed stream is a write error.
std::expected<void, Error> write_body(
    const std::filesystem::path& target, std::FILE* file,
    const std::function<TextBodyResult(std::ostream&)>& body,
    const FaultInjector& fault) {
  StdioStreambuf buffer{file};
  std::ostream out{&buffer};
  if (auto result = body(out); not result) {
    return result;
  }
  out.flush();
  const bool injected = fault.fails(AtomicStage::body);
  if (not out or injected) {
    const std::error_code ec =
        injected ? fault.code
                 : (buffer.error() ? buffer.error()
                                   : std::make_error_code(std::errc::io_error));
    return std::unexpected{Error{file_error(FileOp::write, target, ec)}};
  }
  return {};
}

// Stage 3.
std::expected<void, Error> close_stage(const std::filesystem::path& target,
                                       FilePtr& file,
                                       const FaultInjector& fault) {
  const std::error_code ec = close_file(file);
  if (ec or fault.fails(AtomicStage::close)) {
    return std::unexpected{
        Error{file_error(FileOp::close, target,
                         fault.fails(AtomicStage::close) ? fault.code : ec)}};
  }
  return {};
}

std::uint64_t random_suffix() {
  std::random_device device;
  return (static_cast<std::uint64_t>(device()) << 32U) | device();
}

}  // namespace

std::filesystem::path temp_path_for(
    const std::filesystem::path& target,
    std::optional<std::uint64_t> forced_suffix) {
  const std::uint64_t suffix = forced_suffix ? *forced_suffix : random_suffix();
  return target.parent_path() / std::format(".mov-{:016x}.tmp", suffix);
}

std::expected<void, FileError> check_target_replaceable(
    const std::filesystem::path& target) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::file_status existing = fs::status(target, ec);
  if (ec or existing.type() != fs::file_type::regular) {
    return {};
  }
  if ((existing.permissions() & fs::perms::owner_write) == fs::perms::none) {
    return std::unexpected{
        file_error(FileOp::open, target,
                   std::make_error_code(std::errc::permission_denied))};
  }
  return {};
}

std::expected<void, FileError> commit_temp(const std::filesystem::path& target,
                                           const std::filesystem::path& temp,
                                           const FaultInjector& fault) {
  return copy_permissions(target, temp)
      .and_then([&] {
        return run_stage(AtomicStage::fsync_file, FileOp::fsync, target, fault,
                         [&temp] { return fsync_file_path(temp); });
      })
      .and_then([&] {
        return run_stage(AtomicStage::rename, FileOp::rename, target, fault,
                         [&] { return replace_file(temp, target); });
      })
      .and_then([&] {
        return run_stage(
            AtomicStage::fsync_dir, FileOp::fsync_dir, target, fault,
            [&] { return fsync_directory_path(containing_directory(target)); });
      });
}

std::expected<void, Error> write_text_atomic(
    const std::filesystem::path& target,
    const std::function<TextBodyResult(std::ostream&)>& body,
    const FaultInjector& fault) {
  if (const auto replaceable = check_target_replaceable(target);
      not replaceable) {
    return std::unexpected{Error{replaceable.error()}};
  }
  const std::filesystem::path temp = temp_path_for(target, fault.temp_suffix);
  if (fault.fails(AtomicStage::create)) {
    return std::unexpected{
        Error{file_error(FileOp::create, target, fault.code)}};
  }
  auto opened = open_exclusive(temp);
  if (not opened) {
    return std::unexpected{
        Error{file_error(FileOp::create, target, opened.error())}};
  }
  // Declared before `file`, so the file is closed before it is removed.
  TempFileGuard guard{temp};
  FilePtr file = std::move(*opened);
  if (auto written = write_body(target, file.get(), body, fault); not written) {
    return written;
  }
  if (auto closed = close_stage(target, file, fault); not closed) {
    return closed;
  }
  if (auto committed = commit_temp(target, temp, fault); not committed) {
    return std::unexpected{Error{committed.error()}};
  }
  guard.release();
  return {};
}

}  // namespace mov::io::detail
