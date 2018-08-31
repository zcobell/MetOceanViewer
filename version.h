/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2018  Zach Cobell
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//-----------------------------------------------------------------------*/
#ifndef VERSION_H
#define VERSION_H
#include <string>

#define VER_FILEVERSION 4.0.0
#define VER_FILEVERSION_STR "v4.0.0"

#define VER_PRODUCTVERSION 4.0.0
#define VER_PRODUCTVERSION_STR "v4.0.0"

static std::string metoceanVersion() {
  if (std::string(GIT_VERSION) == std::string("unversioned")) {
    return VER_FILEVERSION_STR;
  } else {
    return GIT_VERSION;
  }
}

#endif  // VERSION_H
