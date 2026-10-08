// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// station_nc_fixtures <directory>: writes the canonical station netCDF files
// (station_nc_canonical.hpp) and the ncdump probe into <directory>, for the
// format-compliance job (tools/check_cf.py). Not a test: it only writes.

#include <exception>
#include <filesystem>
#include <iostream>
#include <span>

#include "nc_header_dump.hpp"
#include "station_nc_canonical.hpp"

int main(int argc, char** argv) {
  const std::span<char*> args{argv, static_cast<std::size_t>(argc)};
  if (args.size() != 2) {
    std::cerr << "usage: station_nc_fixtures <directory>\n";
    return 2;
  }
  try {
    const std::filesystem::path dir{args[1]};
    std::filesystem::create_directories(dir);
    for (const auto& c : mov::test::station_nc::all()) {
      std::cout << mov::test::station_nc::write(c, dir).string() << '\n';
    }
    mov::test::ncgen::make_ncdump_probe(dir / "ncdump_probe.nc");
    std::cout << (dir / "ncdump_probe.nc").string() << '\n';
  } catch (const std::exception& e) {
    std::cerr << "station_nc_fixtures: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
