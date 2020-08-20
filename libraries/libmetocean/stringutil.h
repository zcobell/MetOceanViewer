/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2019  Zach Cobell
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
#ifndef STRINGUTIL_H
#define STRINGUTIL_H

#include <sstream>
#include <string>
#include <vector>

#include "metocean_global.h"

class StringUtil {
 public:
  static METOCEANSHARED_EXPORT std::vector<std::string> stringSplitToVector(
      std::string s, std::string delim, bool useTokenCompress = false);
  static METOCEANSHARED_EXPORT std::vector<std::string> stringSplitToVector(
      std::string s);
  static int METOCEANSHARED_EXPORT stringToInt(std::string a, bool &ok);
  static float METOCEANSHARED_EXPORT stringToFloat(std::string a, bool &ok);
  static double METOCEANSHARED_EXPORT stringToDouble(std::string a, bool &ok);
  static std::string METOCEANSHARED_EXPORT sanitizeString(std::string &a);
};

#endif  // STRINGUTIL_H
