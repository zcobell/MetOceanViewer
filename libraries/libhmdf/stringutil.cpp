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
#include "stringutil.h"
#include "boost/algorithm/string/classification.hpp"
#include "boost/algorithm/string/split.hpp"
#include "boost/algorithm/string/trim.hpp"

vector<string> StringUtil::stringSplitToVector(string s, string delim) {
  vector<string> elems;
  boost::trim_if(s, boost::is_any_of(delim));
  boost::algorithm::split(elems, s, boost::is_any_of(delim),
                          boost::token_compress_on);
  return elems;
}

vector<string> StringUtil::stringSplitToVector(string s) {
  vector<string> elems;
  boost::trim_if(s, boost::is_any_of(" "));
  boost::algorithm::split(elems, s, boost::is_any_of(" "),
                          boost::token_compress_on);
  return elems;
}

double StringUtil::stringToDouble(string a, bool &ok) {
  ok = true;
  try {
    return std::stod(a);
  } catch (...) {
    ok = false;
    return 0.0;
  }
}

float StringUtil::stringToFloat(string a, bool &ok) {
  ok = true;
  try {
    return std::stof(a);
  } catch (...) {
    ok = false;
    return 0.0f;
  }
}

int StringUtil::stringToInt(string a, bool &ok) {
  ok = true;
  try {
    return std::stoi(a);
  } catch (...) {
    ok = false;
    return 0;
  }
}

string StringUtil::sanitizeString(string a) {
  string b = a;
  boost::algorithm::trim(b);
  b.erase(std::remove(b.begin(), b.end(), '\r'), b.end());
  return b;
}
