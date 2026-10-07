// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace mov::test {

/// A fresh empty directory under the system temporary directory, removed with
/// its contents (even read-only ones) when the object goes out of scope.
class ScratchDir {
 public:
  ScratchDir() {
    std::random_device device;
    path_ = std::filesystem::temp_directory_path() /
            std::format("mov-test-{:08x}{:08x}", device(), device());
    std::filesystem::create_directories(path_);
  }
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;
  ScratchDir(ScratchDir&&) = delete;
  ScratchDir& operator=(ScratchDir&&) = delete;
  ~ScratchDir() {
    std::error_code ignored;
    // A test may have removed write permission from a directory.
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator{path_, ignored}) {
      if (entry.is_directory(ignored)) {
        std::filesystem::permissions(
            entry.path(), std::filesystem::perms::owner_all,
            std::filesystem::perm_options::add, ignored);
      }
    }
    std::filesystem::permissions(path_, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::add, ignored);
    std::filesystem::remove_all(path_, ignored);
  }

  [[nodiscard]] const std::filesystem::path& path() const& noexcept {
    return path_;
  }
  [[nodiscard]] std::filesystem::path operator/(std::string_view name) const {
    return path_ / name;
  }

 private:
  std::filesystem::path path_;
};

/// Writes `bytes` to `path`, replacing it.
inline void write_bytes(const std::filesystem::path& path,
                        std::string_view bytes) {
  std::ofstream out{path, std::ios::binary | std::ios::trunc};
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

/// The whole file, or "" when it cannot be read.
[[nodiscard]] inline std::string read_bytes(const std::filesystem::path& path) {
  std::ifstream in{path, std::ios::binary};
  std::ostringstream content;
  content << in.rdbuf();
  return content.str();
}

/// The names of the entries of `dir`, sorted.
[[nodiscard]] inline std::vector<std::string> entry_names(
    const std::filesystem::path& dir) {
  std::vector<std::string> names;
  for (const auto& entry : std::filesystem::directory_iterator{dir}) {
    names.push_back(entry.path().filename().string());
  }
  std::ranges::sort(names);
  return names;
}

}  // namespace mov::test
