/*  $Id: Amplitude.hh 5748 2014-10-11 19:38:53Z flaterco $

    Amplitude:  A non-negative quantity in units of feet, meters,
    knots, or knots squared.  See also, PredictionValue.

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


// The relationship between Amplitude and PredictionValue is
// completely analogous to the famous (or infamous) Square and
// Rectangle example that everybody gets wrong.

// Amplitude is not a subclass of PredictionValue.  Here's the proof:

//   void Munge (PredictionValue &foo) { foo *= -2.0; }

//   Amplitude bar (meters, 3.0);
//   Munge (bar);

// Either the attempt to munge bar must fail, in which case
// substitutability has been violated, or bar must morph into a
// PredictionValue, which is impossible in C++.

// If you take away mutability from both Amplitude and
// PredictionValue, then the subclassing is valid, but I want my
// Amplitudes and PredictionValues to be mutable.  Consequently, an
// Amplitude is not a PredictionValue, but an implicit conversion from
// Amplitude to PredictionValue is available.

namespace libxtide {

class Amplitude {
public:

  Amplitude (); // Initialize to 0 Zulu.  (See PredictionValue.)

  // It is an error if value is less than zero.
  Amplitude (Units::PredictionUnits units, double value);

  inline const double val() const {
    return pv.val();
  }

  void Units (Units::PredictionUnits units);

  inline const Units::PredictionUnits Units () const {
    return pv.Units();
  }

  void operator+= (Amplitude argument);

  // It is an error if levelMultiply is less than zero.
  void operator*= (double levelMultiply);

protected:
  PredictionValue pv;

  friend const bool operator> (Amplitude a, Amplitude b);
  friend const bool operator< (Amplitude a, Amplitude b);
};

// It is an error if a is less than zero.
const Amplitude operator* (double a, Amplitude b);
const Amplitude operator* (Amplitude b, double a);

}
