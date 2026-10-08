// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/text_file.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

#include "file_handle.hpp"

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace mov::io {

namespace {

FileError file_error(FileOp op, const std::filesystem::path& path,
                     std::error_code ec) {
  return FileError{.op = op, .path = path, .ec = ec};
}

std::unexpected<FileError> failure(FileOp op, const std::filesystem::path& path,
                                   std::error_code ec) {
  return std::unexpected{file_error(op, path, ec)};
}

// The kinds of file a text file can be.
enum class Kind : std::uint8_t { regular, directory, other };

std::error_code not_regular_code(Kind kind) {
  return std::make_error_code(kind == Kind::directory
                                  ? std::errc::is_a_directory
                                  : std::errc::invalid_argument);
}

#if defined(_WIN32)

std::error_code last_error_code() noexcept {
  return std::error_code{static_cast<int>(::GetLastError()),
                         std::system_category()};
}

// Opens, asks the open handle what it is and how big, and reads it.
std::expected<std::string, FileError> read_open_file(
    const std::filesystem::path& path, const ReadLimits& limits,
    std::optional<std::uintmax_t> prefix) {
  // BACKUP_SEMANTICS lets a directory be opened, to be recognized and refused.
  const detail::FileHandle file{::CreateFileW(
      path.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS,
      nullptr)};
  if (not file.is_open()) {
    return failure(FileOp::open, path, last_error_code());
  }
  BY_HANDLE_FILE_INFORMATION info{};
  if (::GetFileInformationByHandle(file.get(), &info) == 0) {
    return failure(FileOp::size, path, last_error_code());
  }
  if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return failure(FileOp::open, path, not_regular_code(Kind::directory));
  }
  if (::GetFileType(file.get()) != FILE_TYPE_DISK) {
    return failure(FileOp::open, path, not_regular_code(Kind::other));
  }
  std::uintmax_t size =
      (static_cast<std::uintmax_t>(info.nFileSizeHigh) << 32U) |
      info.nFileSizeLow;
  if (prefix) {
    size = std::min(size, *prefix);
  } else if (size > limits.max_text_bytes) {
    return failure(FileOp::size, path,
                   std::make_error_code(std::errc::file_too_large));
  }
  std::string content(static_cast<std::size_t>(size), '\0');
  std::size_t total = 0;
  while (total < content.size()) {
    constexpr std::size_t chunk = std::size_t{1} << 30U;
    const auto wanted =
        static_cast<DWORD>(std::min(content.size() - total, chunk));
    DWORD got = 0;
    if (::ReadFile(file.get(), content.data() + total, wanted, &got, nullptr) ==
        0) {
      return failure(FileOp::read, path, last_error_code());
    }
    if (got == 0) {
      break;  // shrunk while read
    }
    total += got;
  }
  content.resize(total);
  return content;
}

#else

Kind kind_of(const struct stat& info) noexcept {
  if (S_ISREG(info.st_mode)) {
    return Kind::regular;
  }
  return S_ISDIR(info.st_mode) ? Kind::directory : Kind::other;
}

std::error_code errno_code() noexcept {
  return errno != 0 ? std::error_code{errno, std::generic_category()}
                    : std::make_error_code(std::errc::io_error);
}

// Reads up to `size` bytes from `fd`, retrying interrupted calls. Stops early
// at the end of the file.
std::expected<std::string, std::error_code> read_all(int fd, std::size_t size) {
  std::string content(size, '\0');
  std::size_t total = 0;
  while (total < size) {
    errno = 0;
    const ssize_t got = ::read(fd, content.data() + total, size - total);
    if (got < 0 and errno == EINTR) {
      continue;
    }
    if (got < 0) {
      return std::unexpected{errno_code()};
    }
    if (got == 0) {
      break;  // shrunk while read
    }
    total += static_cast<std::size_t>(got);
  }
  content.resize(total);
  return content;
}

// Opens, asks the open descriptor what it is and how big, and reads it.
// O_NONBLOCK so that opening a FIFO does not wait for a writer.
std::expected<std::string, FileError> read_open_file(
    const std::filesystem::path& path, const ReadLimits& limits,
    std::optional<std::uintmax_t> prefix) {
  errno = 0;
  const detail::FileHandle file{
      ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC)};
  if (not file.is_open()) {
    return failure(FileOp::open, path, errno_code());
  }
  struct stat info{};
  errno = 0;
  if (::fstat(file.get(), &info) != 0) {
    return failure(FileOp::size, path, errno_code());
  }
  if (const Kind kind = kind_of(info); kind != Kind::regular) {
    return failure(FileOp::open, path, not_regular_code(kind));
  }
  auto size = static_cast<std::uintmax_t>(info.st_size);
  if (prefix) {
    size = std::min(size, *prefix);
  } else if (size > limits.max_text_bytes) {
    return failure(FileOp::size, path,
                   std::make_error_code(std::errc::file_too_large));
  }
  auto content = read_all(file.get(), static_cast<std::size_t>(size));
  if (not content) {
    return failure(FileOp::read, path, content.error());
  }
  return *std::move(content);
}

#endif

}  // namespace

std::expected<std::string, FileError> read_text_file(
    const std::filesystem::path& path, const ReadLimits& limits) {
  return read_open_file(path, limits, std::nullopt);
}

std::expected<std::string, FileError> read_text_prefix(
    const std::filesystem::path& path, std::size_t max_bytes) {
  return read_open_file(path, ReadLimits{}, std::uintmax_t{max_bytes});
}

}  // namespace mov::io
