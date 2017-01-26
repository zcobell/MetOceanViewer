// $Id: HarmonicsPath.hh 5748 2014-10-11 19:38:53Z flaterco $

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


namespace libxtide {

// "STL containers are not intended to be used as base classes (their
// destructors are deliberately non-virtual).  Deriving from a
// container is a common mistake made by novices."
// -- Standard Template Library,
// http://en.wikipedia.org/w/index.php?title=Standard_Template_Library&oldid=98705028
// (last visited January 13, 2007).

class HarmonicsPath: public SafeVector<Dstr> {
public:
  HarmonicsPath (const Dstr &unparsedHfilePath);

  // HarmonicsPath defaults to harmonics.tcd in the current directory
  // if no path is provided, but to give accurate error messages we
  // need to distinguish the bad-path-provided case from the
  // no-path-provided case.
  const bool noPathProvided();

  // Return original, un-parsed path for use in error messages.
  constString origPath();

protected:
  bool _noPathProvided;
  Dstr _origPath;
};

}
