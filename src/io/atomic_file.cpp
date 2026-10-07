// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/detail/atomic_file.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <optional>
#include <ostream>
#include <random>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN  // NOLINT(cppcoreguidelines-macro-usage)
#endif
#ifndef NOMINMAX
#define NOMINMAX  // NOLINT(cppcoreguidelines-macro-usage)
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace mov::io::detail {

namespace {

FileError file_error(FileOp op, const std::filesystem::path& target,
                     std::error_code ec) {
  return FileError{.op = op, .path = target, .ec = ec};
}

std::error_code errno_code() noexcept {
  return std::error_code{errno, std::generic_category()};
}

#if defined(_WIN32)

std::error_code last_error_code() noexcept {
  return std::error_code{static_cast<int>(::GetLastError()),
                         std::system_category()};
}

// Flushes the file's buffers to the disk (FlushFileBuffers).
std::error_code fsync_file_path(const std::filesystem::path& file) noexcept {
  const HANDLE handle =
      ::CreateFileW(file.c_str(), GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return last_error_code();
  }
  const bool flushed = ::FlushFileBuffers(handle) != 0;
  const std::error_code ec = flushed ? std::error_code{} : last_error_code();
  ::CloseHandle(handle);
  return ec;
}

// NTFS makes a rename durable through MOVEFILE_WRITE_THROUGH; there is no
// directory to flush.
std::error_code fsync_directory_path(const std::filesystem::path&) noexcept {
  return {};
}

std::error_code replace_file(const std::filesystem::path& from,
                             const std::filesystem::path& to) noexcept {
  const bool moved =
      ::MoveFileExW(from.c_str(), to.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
  return moved ? std::error_code{} : last_error_code();
}

#else

// Owns a POSIX file descriptor.
class FileDescriptor {
 public:
  explicit FileDescriptor(int fd) noexcept : fd_{fd} {}
  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;
  FileDescriptor(FileDescriptor&&) = delete;
  FileDescriptor& operator=(FileDescriptor&&) = delete;
  ~FileDescriptor() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }
  [[nodiscard]] int get() const noexcept { return fd_; }

 private:
  int fd_;
};

// fsync of a file or, with O_DIRECTORY, of a directory. macOS fsync does not
// flush the drive's cache; F_FULLFSYNC does, and a filesystem that rejects it
// falls back to fsync.
std::error_code fsync_path(const std::filesystem::path& p,
                           int extra_flags) noexcept {
  const FileDescriptor fd{
      ::open(p.c_str(), O_RDONLY | O_CLOEXEC | extra_flags)};
  if (fd.get() < 0) {
    return errno_code();
  }
#if defined(F_FULLFSYNC)
  if (::fcntl(fd.get(), F_FULLFSYNC) == 0) {
    return {};
  }
#endif
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

// What to flush after a rename: the directory that holds the target.
std::filesystem::path containing_directory(
    const std::filesystem::path& target) {
  const std::filesystem::path parent = target.parent_path();
  return parent.empty() ? std::filesystem::path{"."} : parent;
}

// Runs `stage` unless the injector fails it first.
template <class Operation>
std::expected<void, FileError> run_stage(AtomicStage stage, FileOp op,
                                         const std::filesystem::path& target,
                                         const FaultInjector& fault,
                                         const Operation& operation) {
  const std::error_code ec =
      fault.fails(stage) ? injected_fault_code() : operation();
  if (ec) {
    return std::unexpected{file_error(op, target, ec)};
  }
  return {};
}

std::error_code create_empty_exclusively(const std::filesystem::path& temp) {
#if defined(_WIN32)
  std::FILE* const raw = ::_wfopen(temp.c_str(), L"wbx");
#else
  std::FILE* const raw = std::fopen(temp.c_str(), "wbx");
#endif
  if (raw == nullptr) {
    return errno_code();
  }
  // The file exists; a failing close is the only thing left to report, and
  // nothing else will remove it.
  if (std::fclose(raw) != 0) {
    const std::error_code ec = errno_code();
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);
    return ec;
  }
  return {};
}

// Stages 2 and 3 for a std::ofstream.
std::expected<void, FileError> write_and_close(
    const std::filesystem::path& target, const std::filesystem::path& temp,
    const std::function<void(std::ostream&)>& body,
    const FaultInjector& fault) {
  std::ofstream out{temp, std::ios::binary | std::ios::trunc};
  if (not out) {
    return std::unexpected{file_error(FileOp::open, target, errno_code())};
  }
  body(out);
  out.flush();
  if (not out or fault.fails(AtomicStage::body)) {
    return std::unexpected{file_error(
        FileOp::write, target, std::make_error_code(std::errc::io_error))};
  }
  out.close();
  if (out.fail() or fault.fails(AtomicStage::close)) {
    return std::unexpected{file_error(
        FileOp::close, target, std::make_error_code(std::errc::io_error))};
  }
  return {};
}

}  // namespace

std::error_code injected_fault_code() noexcept {
  return std::make_error_code(std::errc::io_error);
}

std::filesystem::path temp_path_for(
    const std::filesystem::path& target,
    std::optional<std::uint64_t> forced_suffix) {
  std::uint64_t suffix = 0;
  if (forced_suffix) {
    suffix = *forced_suffix;
  } else {
    std::random_device device;
    suffix = (static_cast<std::uint64_t>(device()) << 32U) | device();
  }
  std::filesystem::path temp = target;
  temp += std::format(".{:016x}.tmp", suffix);
  return temp;
}

std::expected<void, FileError> create_exclusive(
    const std::filesystem::path& target, const std::filesystem::path& temp,
    const FaultInjector& fault) {
  return run_stage(AtomicStage::create, FileOp::create, target, fault,
                   [&temp] { return create_empty_exclusively(temp); });
}

std::expected<void, FileError> commit_temp(const std::filesystem::path& target,
                                           const std::filesystem::path& temp,
                                           const FaultInjector& fault) {
  if (auto synced = run_stage(AtomicStage::fsync_file, FileOp::fsync, target,
                              fault, [&temp] { return fsync_file_path(temp); });
      not synced) {
    return synced;
  }
  if (auto renamed =
          run_stage(AtomicStage::rename, FileOp::rename, target, fault,
                    [&] { return replace_file(temp, target); });
      not renamed) {
    return renamed;
  }
  return run_stage(AtomicStage::fsync_dir, FileOp::fsync, target, fault, [&] {
    return fsync_directory_path(containing_directory(target));
  });
}

std::expected<void, FileError> write_text_atomic(
    const std::filesystem::path& target,
    const std::function<void(std::ostream&)>& body,
    const FaultInjector& fault) {
  const std::filesystem::path temp = temp_path_for(target, fault.temp_suffix);
  if (auto created = create_exclusive(target, temp, fault); not created) {
    return created;  // nothing was created, or someone else's file is there
  }
  TempFileGuard guard{temp};
  if (auto written = write_and_close(target, temp, body, fault); not written) {
    return written;
  }
  if (auto committed = commit_temp(target, temp, fault); not committed) {
    return committed;
  }
  guard.release();
  return {};
}

}  // namespace mov::io::detail
