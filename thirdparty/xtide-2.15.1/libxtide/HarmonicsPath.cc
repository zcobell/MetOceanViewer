// $Id: HarmonicsPath.cc 5748 2014-10-11 19:38:53Z flaterco $

/*  HarmonicsPath  Vector of harmonics file names as specified by environment.

    Copyright (C) 1997  David Flater.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "libxtide.hh"
#include "HarmonicsPath.hh"
namespace libxtide {


#ifdef UseSemicolonPathsep
static const char pathSeparator (';');
#else
static const char pathSeparator (':');
#endif


HarmonicsPath::HarmonicsPath (const Dstr &unparsedHfilePath):
  _noPathProvided(false) {

  Dstr hfile_path (unparsedHfilePath);
  _origPath = hfile_path;
  if (hfile_path.isNull()) {
    _noPathProvided = true;
    hfile_path = "harmonics.tcd";
  }

  // Trying to tolerate whitespace in file names.
  while (hfile_path.length()) {
    int i = hfile_path.strchr (pathSeparator);
    if (i == 0)
      hfile_path /= 1;
    else if (i < 0) {
      push_back (hfile_path);
      return;
    } else {
      Dstr temp (hfile_path);
      temp -= i;
      push_back (temp);
      hfile_path /= i+1;
    }
  }
}


const bool HarmonicsPath::noPathProvided() {
  return _noPathProvided;
}


constString HarmonicsPath::origPath() {
  return _origPath.aschar();
}

}
