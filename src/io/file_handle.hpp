// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Owning wrappers for the file handles of the platform, private to the .cpp
// files of mov_io.

#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN  // NOLINT(cppcoreguidelines-macro-usage)
#endif
#ifndef NOMINMAX
#define NOMINMAX  // NOLINT(cppcoreguidelines-macro-usage)
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace mov::io::detail {

#if defined(_WIN32)

/// A Windows file HANDLE, closed on destruction.
class FileHandle {
 public:
  explicit FileHandle(HANDLE handle) noexcept : handle_{handle} {}
  FileHandle(const FileHandle&) = delete;
  FileHandle& operator=(const FileHandle&) = delete;
  FileHandle(FileHandle&&) = delete;
  FileHandle& operator=(FileHandle&&) = delete;
  ~FileHandle() {
    if (is_open()) {
      ::CloseHandle(handle_);
    }
  }
  [[nodiscard]] bool is_open() const noexcept {
    return handle_ != INVALID_HANDLE_VALUE;
  }
  [[nodiscard]] HANDLE get() const noexcept { return handle_; }

 private:
  HANDLE handle_;
};

#else

/// A POSIX file descriptor, closed on destruction.
class FileHandle {
 public:
  explicit FileHandle(int fd) noexcept : fd_{fd} {}
  FileHandle(const FileHandle&) = delete;
  FileHandle& operator=(const FileHandle&) = delete;
  FileHandle(FileHandle&&) = delete;
  FileHandle& operator=(FileHandle&&) = delete;
  ~FileHandle() {
    if (is_open()) {
      ::close(fd_);
    }
  }
  [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }
  [[nodiscard]] int get() const noexcept { return fd_; }

 private:
  int fd_;
};

#endif

}  // namespace mov::io::detail
