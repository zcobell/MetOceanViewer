// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/text_file.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>

namespace mov::io {

namespace {

FileError file_error(FileOp op, const std::filesystem::path& path,
                     std::error_code ec) {
  return FileError{.op = op, .path = path, .ec = ec};
}

// The error of a path that cannot be read as text before any byte is read,
// or nullopt for a regular file.
std::optional<FileError> check_regular_file(const std::filesystem::path& path) {
  std::error_code ec;
  const std::filesystem::file_status status = std::filesystem::status(path, ec);
  switch (status.type()) {
    case std::filesystem::file_type::regular:
      return std::nullopt;
    case std::filesystem::file_type::not_found:
      return file_error(
          FileOp::open, path,
          ec ? ec : std::make_error_code(std::errc::no_such_file_or_directory));
    case std::filesystem::file_type::directory:
      return file_error(FileOp::open, path,
                        std::make_error_code(std::errc::is_a_directory));
    default:
      return file_error(
          FileOp::open, path,
          ec ? ec : std::make_error_code(std::errc::invalid_argument));
  }
}

}  // namespace

std::expected<std::string, FileError> read_text_file(
    const std::filesystem::path& path, const ReadLimits& limits) {
  if (const auto problem = check_regular_file(path)) {
    return std::unexpected{*problem};
  }
  std::error_code ec;
  const std::uintmax_t size = std::filesystem::file_size(path, ec);
  if (ec) {
    return std::unexpected{file_error(FileOp::size, path, ec)};
  }
  if (size > limits.max_text_bytes) {
    return std::unexpected{file_error(
        FileOp::size, path, std::make_error_code(std::errc::file_too_large))};
  }
  std::ifstream in{path, std::ios::binary};
  if (not in) {
    return std::unexpected{file_error(
        FileOp::open, path, std::error_code{errno, std::generic_category()})};
  }
  std::string content(static_cast<std::size_t>(size), '\0');
  in.read(content.data(), static_cast<std::streamsize>(content.size()));
  if (in.bad()) {
    return std::unexpected{file_error(
        FileOp::read, path, std::make_error_code(std::errc::io_error))};
  }
  content.resize(static_cast<std::size_t>(in.gcount()));  // shrunk while read
  return content;
}

}  // namespace mov::io
