// $Id: Colors.hh 5748 2014-10-11 19:38:53Z flaterco $

/*  Colors  Manage XTide colors without X.

    Copyright (C) 1998  David Flater.

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

namespace Colors {

  const unsigned numColors = 12U;

  enum Colorchoice {background=0,
                    foreground=1,
                    mark=2,
                    button=3,
                    daytime=4,
                    nighttime=5,
                    flood=6,
                    ebb=7,
                    datum=8,
                    msl=9,
                    tidedot=10,
                    currentdot=11};

  extern constString colorarg[numColors]; // Wants to be [Colorchoice]

  // This is an X-less implementation of XParseColor.  It understands
  // the standard color names from rgb.txt and colors of the form
  // rgb:hh/hh/hh (24-bit color specs only).
  // Set fatality to determine whether an unparsed color is a fatal error
  // or not.  Returns true on success.
  const bool parseColor (const Dstr &colorName,
			 uint8_t &r,
			 uint8_t &g,
			 uint8_t &b,
			 Error::ErrType fatality = Error::fatal);
}

}
