// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Writes tests/fixtures/core/tide/congen_golden.csv (its data rows; the
// header comment is maintained by hand): speeds, (V0+u) and f per year from
// libcongen, an independent implementation of the SP 98 yearly scheme, for
// every catalogue name. Not built by CMake or CI; see tools/tide/README.md.
//
// libcongen: congen 1.7 by David Flater, GPL-3.
//   https://flaterco.com/files/xtide/congen-1.7-r2.tar.xz
//   sha256 1dbae015e338e5780a505ebf233058f5e91628319a11bfbc465cb556cfb4b0d7
//   (fetched 2026-10-08). Build it with ./configure --disable-shared; make.
//
// Usage:
//   g++ -O2 -std=c++17 -I congen-1.7 -o congen_golden congen_golden.cpp \
//       congen-1.7/.libs/libcongen.a -lm
//   congen_golden congen-1.7/congen_input.txt names.txt 1700 1750 ... 2200
// names.txt holds the catalogue names in catalogue order, one per line.
// Catalogue names congen spells differently are looked up under congen's
// name (the alias table below; RP1 and KJ2 are the phase readings the
// catalogue documents).

#include <Congen>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s congen_input.txt names.txt year...\n",
                 argv[0]);
    return 2;
  }
  std::ifstream in(argv[1]);
  std::vector<Congen::Constituent> constituents;
  if (Congen::parseLegacyInput(in, 1700, 2200, 1900, constituents) != 0) {
    std::fprintf(stderr, "cannot parse %s\n", argv[1]);
    return 1;
  }
  std::map<std::string, const Congen::Constituent*> by_name;
  for (const auto& c : constituents) {
    by_name[c.name] = &c;
  }
  const std::map<std::string, std::string> congen_name{
      {"LAM2", "LDA2"},       {"RHO", "RHO1"},        {"SIGMA1", "SIG1"},
      {"THETA1", "THE1"},     {"OO2", "OQ2-HORN"},    {"2MS8", "2(MS)8"},
      {"2MN8", "2(MN)8"},     {"2MLNS6", "2MNLS6"},   {"M2KS2", "M2(KS)2"},
      {"2SNMK2", "2SN(MK)2"}, {"2KMSN2", "2KM(SN)2"}, {"RP1", "PSI1"},
      {"KJ2", "KJ2-IHO"}};
  std::vector<int> years;
  for (int i = 3; i < argc; ++i) {
    years.push_back(std::atoi(argv[i]));
  }
  std::ifstream names(argv[2]);
  std::string name;
  std::printf("name,year,speed_deg_per_hour,v0_plus_u_deg,f\n");
  while (names >> name) {
    const auto alias = congen_name.find(name);
    const std::string looked_up =
        alias == congen_name.end() ? name : alias->second;
    const auto found = by_name.find(looked_up);
    if (found == by_name.end()) {
      std::fprintf(stderr, "congen has no %s\n", looked_up.c_str());
      return 1;
    }
    const Congen::Constituent& c = *found->second;
    for (const int y : years) {
      // libcongen never reduces (V0+u); fmod is exact.
      double argument = std::fmod(c.equilibriumArgument[y - 1700], 360.0);
      if (argument < 0) {
        argument += 360.0;
      }
      std::printf("%s,%d,%.10f,%.10f,%.12f\n", name.c_str(), y, c.speed,
                  argument, c.f[y - 1700]);
    }
  }
  return 0;
}
