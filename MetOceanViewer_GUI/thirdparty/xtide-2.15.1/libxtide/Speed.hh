// $Id: Speed.hh 5748 2014-10-11 19:38:53Z flaterco $

// Speed:  angular units over time units.

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

class Speed {
public:

  // Value is initialized in degrees per hour (what we get from libtcd).
  explicit Speed (double degreesPerHour);

  // Value is returned in radians per second (what we actually use).
  inline const double radiansPerSecond() const {
    return _radiansPerSecond;
  }

protected:
  double _radiansPerSecond;
};

}
