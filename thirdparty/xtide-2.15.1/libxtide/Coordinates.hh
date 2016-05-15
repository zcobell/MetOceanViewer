// $Id: Coordinates.hh 5748 2014-10-11 19:38:53Z flaterco $

/*  Coordinates:  Degrees latitude and longitude.

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

class Coordinates: public Nullable {
public:
  Coordinates (); // Creates null Coordinates.

  // lat range -90 to 90; lng range -180 to 180.
  Coordinates (double lat, double lng);

  // It is an error to invoke either of the following methods when null.
  const double lat() const;
  const double lng() const;

  // It is permissible to print nulls.
  // These functions always use the Latin-1 degree sign.  If and when
  // necessary, it is replaced with the UTF-8 or VT100 sequence by the
  // caller.
  enum Pad {noPadding, fixedWidth};
  void print    (Dstr &text_out,
                 Pad pad = noPadding) const;  // X.XXXX N/S, X.XXXX E/W
  void printLat (Dstr &text_out) const;       // X.X N/S
  void printLng (Dstr &text_out) const;       // X.X E/W

protected:
  double latitude;
  double longitude;
};

}
