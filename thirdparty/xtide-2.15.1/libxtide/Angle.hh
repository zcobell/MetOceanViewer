// $Id: Angle.hh 5748 2014-10-11 19:38:53Z flaterco $

// Angle

/*
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

class Angle {
public:
  Angle (); // Creates a zero angle.

  inline Angle (Units::AngleUnits units, double value):
    radians((units == Units::degrees) ? (value * M_PI / 180.0) : value) {}

  inline void operator+= (Angle a) {
    radians += a.radians;
  }

  void operator-= (Angle a);

protected:

  double radians;

  // XTide never needs the actual angle--it only needs the sine or
  // cosine of it.  So if the value is hidden from everything except
  // sin and cos, we can dispense with normalization.
  friend const double sin (Angle a);
  friend const double cos (Angle a);
};

inline const double cos (Angle a) {
  return ::cos (a.radians);
}

inline const Angle operator+ (Angle a, Angle b) {
  a += b;
  return a;
}

}
