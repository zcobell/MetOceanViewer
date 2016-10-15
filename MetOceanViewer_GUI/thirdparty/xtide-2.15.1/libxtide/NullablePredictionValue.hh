// $Id: NullablePredictionValue.hh 5748 2014-10-11 19:38:53Z flaterco $

// NullablePredictionValue:  strict nullable PredictionValue.

/*
    Copyright (C) 2006  David Flater.

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

// If not null, an implicit conversion to PredictionValue is available.

namespace libxtide {

class NullablePredictionValue: public Nullable {
public:
  NullablePredictionValue ();                               // Makes a null.
  NullablePredictionValue (PredictionValue pv);             // Not null.
  NullablePredictionValue (Units::PredictionUnits units,    // Not null.
                           double value);

  const double val() const;                     // Error if null.
  void Units (Units::PredictionUnits units);    // Error if null.
  const Units::PredictionUnits Units () const;  // Error if null.

  // Let int types be implicitly converted to double.
  void operator*= (double multiplier);          // Error if null.
  void operator/= (double divisor);             // Error if null.

  // Like += except that conversions are silently performed.
  // Use only when you really mean it.
  void convertAndAdd (PredictionValue addend);  // Error if null.

  // Print in the form -XX.YY units (padding as needed).  Error if null.
  void print (Dstr &text_out) const;
  // Same thing without padding, with abbreviated units.  Error if null.
  void printnp (Dstr &text_out) const;

protected:
  PredictionValue _pv;
};

}
