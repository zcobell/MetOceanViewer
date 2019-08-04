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

using namespace std;

class StringUtil {
public:
  static vector<string> stringSplitToVector(string s, string delim);
  static vector<string> stringSplitToVector(string s);
  static int stringToInt(std::string a, bool &ok);
  static float stringToFloat(std::string a, bool &ok);
  static double stringToDouble(std::string a, bool &ok);
  static std::string sanitizeString(std::string &a);
};

#endif // STRINGUTIL_H
