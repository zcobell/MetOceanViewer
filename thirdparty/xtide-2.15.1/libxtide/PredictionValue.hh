// $Id: PredictionValue.hh 5748 2014-10-11 19:38:53Z flaterco $
/*
    PredictionValue:  A quantity in units of feet, meters, knots, or
    knots squared.  See also, Amplitude.

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

class Amplitude;
class NullablePredictionValue;


// PredictionValues have a distinct initial state called 0 Zulu.
// 0 Zulu can be converted to any units, whether length or velocity.
// 0 Zulu can be added to any other PredictionValue regardless of its units.
// 0 Zulu will adopt the units of any PredictionValue added to it.
// 0 Zulu is comparable with any PredictionValue regardless of its units.

class PredictionValue {
public:
  PredictionValue (); // Initialize to 0 Zulu.
  PredictionValue (Units::PredictionUnits units, double value);
  PredictionValue (Amplitude a);
  PredictionValue (NullablePredictionValue npv); // Error if null.

  inline const double val() const {
    return _value;
  }

  void Units (Units::PredictionUnits units);

  inline const Units::PredictionUnits Units () const {
    return _units;
  }

  // Let int types be implicitly converted to double.
  inline void operator*= (double multiplier) {
    _value *= multiplier;
  }

  void operator/= (double divisor);

  // With the exception of the 0 Zulu behaviors discussed above, these
  // operators insist that both values must have the same units.
  inline void operator+= (PredictionValue addend) {
    if (addend._units == Units::zulu)
      assert (addend._value == 0.0);
    else if (_units == Units::zulu) {
      assert (_value == 0.0);
      operator= (addend);  // Adopt units of addend
    } else {
      assert (_units == addend._units);
      _value += addend._value;
    }
  }

  void operator-= (PredictionValue subtrahend);

  // Like += except that conversions are silently performed.
  // Use only when you really mean it.
  void convertAndAdd (PredictionValue addend);

  // Print in the form -XX.YY units (padding as needed)
  void print (Dstr &text_out) const;
  // Same thing without padding, with abbreviated units.
  void printnp (Dstr &text_out) const;

protected:
  double _value;
  Units::PredictionUnits _units;
};

// Let int types be implicitly converted to double.
inline const PredictionValue operator* (double a, PredictionValue b) {
  b *= a;
  return b;
}

inline const PredictionValue operator* (PredictionValue b, double a) {
  b *= a;
  return b;
}

const PredictionValue operator/ (PredictionValue b, double a);

// These work as long as the units are comparable.
const bool operator>  (PredictionValue a, PredictionValue b);
const bool operator<  (PredictionValue a, PredictionValue b);
const bool operator>= (PredictionValue a, PredictionValue b);
const bool operator<= (PredictionValue a, PredictionValue b);
const bool operator== (PredictionValue a, PredictionValue b);
const bool operator!= (PredictionValue a, PredictionValue b);

// These insist that both values must have the same units.
// 0 Zulu semantics are not (yet) implemented for these operations.
const PredictionValue operator+ (PredictionValue a, PredictionValue b);
const PredictionValue operator- (PredictionValue a, PredictionValue b);
const double          operator/ (PredictionValue a, PredictionValue b);

const PredictionValue operator- (PredictionValue a);
const PredictionValue abs       (PredictionValue a);

}
