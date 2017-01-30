// $Id: Interval.hh 5748 2014-10-11 19:38:53Z flaterco $

// Interval:  what you get if you subtract two timestamps.

/*
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

class NullableInterval;

// There is no overflow checking on Intervals.

class Interval {
public:
  Interval (); // Initialize to 0 s.
  Interval (interval_rep_t s);
  Interval (const NullableInterval &interval); // Error if null.

  // Takes meridian string of the form [-]HH:MM
  Interval (const Dstr &meridian);

  // Return value in seconds.
  inline const interval_rep_t s() const {
    return seconds;
  }

  void operator *= (unsigned a);

protected:
  interval_rep_t seconds;
};

inline const Angle operator* (Interval a, Speed b) {
  return Angle (Units::radians, a.s() * b.radiansPerSecond());
}

inline const Angle operator* (Speed b, Interval a) {
  return Angle (Units::radians, a.s() * b.radiansPerSecond());
}

const Interval operator+ (Interval a, Interval b);
const Interval operator- (Interval a, Interval b);
const Interval operator* (Interval a, unsigned b);
const Interval operator* (Interval a, double b);
const Interval operator/ (Interval a, int b);
const double   operator/ (Interval a, Interval b);

const Interval abs       (Interval a);
const Interval operator- (Interval a);

const bool operator>  (Interval a, Interval b);
const bool operator<  (Interval a, Interval b);
const bool operator<= (Interval a, Interval b);
const bool operator>= (Interval a, Interval b);
const bool operator== (Interval a, Interval b);
const bool operator!= (Interval a, Interval b);

}
