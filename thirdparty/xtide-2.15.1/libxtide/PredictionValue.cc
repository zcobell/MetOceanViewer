// $Id: PredictionValue.cc 5748 2014-10-11 19:38:53Z flaterco $
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

#include "libxtide.hh"
namespace libxtide {


PredictionValue::PredictionValue ():
  _value(0.0),
  _units(Units::zulu) {}


PredictionValue::PredictionValue (Units::PredictionUnits units,
                                  double value):
  _value(value),
  _units(units) {
  assert (value == 0.0 || units != Units::zulu);
}


PredictionValue::PredictionValue (Amplitude a):
  _value(a.val()),
  _units(a.Units()) {}


PredictionValue::PredictionValue (NullablePredictionValue npv):
  _value(npv.val()),
  _units(npv.Units()) {}


// No return.
static void convbarf (Units::PredictionUnits fromunits,
                      Units::PredictionUnits tounits) {
  Dstr details ("From ");
  details += Units::longName (fromunits);
  details += " to ";
  details += Units::longName (tounits);
  Global::barf (Error::IMPOSSIBLE_CONVERSION, details);
}


void PredictionValue::Units (Units::PredictionUnits units) {
  if (_units == units)
    Global::barf (Error::NO_CONVERSION, Error::nonfatal);
  else {
    switch (_units) {
    case Units::zulu:
      assert (_value == 0.0);
      break;
    case Units::feet:
      if (units == Units::meters)
	_value *= 0.3048;
      else
	convbarf (_units, units);
      break;
    case Units::meters:
      if (units == Units::feet)
	_value /= 0.3048;
      else
	convbarf (_units, units);
      break;
    case Units::knotsSquared:
      if (units == Units::knots) {
	// This is not mathematically correct, but it is tidematically correct.
	if (_value < 0)
	  _value = -sqrt(fabs(_value));
	else
	  _value = sqrt(_value);
      } else
	convbarf (_units, units);
      break;
    case Units::knots:
      if (units == Units::knotsSquared) {
	// This is used only in Station::predictTideEvents to set up
	// the mark level.
	// This is not mathematically correct, but it is tidematically correct.
	if (_value < 0)
	  _value = -(_value*_value);
	else
	  _value *= _value;
      } else
	convbarf (_units, units);
      break;
    default:
      assert (false);
    }
    _units = units;
  }
}


void PredictionValue::operator-= (PredictionValue subtrahend) {
  operator+= (-subtrahend);
}


void PredictionValue::operator/= (double divisor) {
  _value /= divisor;
}


void PredictionValue::convertAndAdd (PredictionValue addend) {
  if (addend._units == Units::zulu) {
    assert (addend._value == 0.0);
    return;
  }
  if (_units != Units::zulu && _units != addend._units)
    addend.Units (_units);
  operator+= (addend);
}


// These insist that both values must have exactly the same units.
// 0 Zulu semantics are not (yet) implemented for these operations.
const PredictionValue operator+ (PredictionValue a, PredictionValue b) {
  assert (a.Units() == b.Units());
  return PredictionValue (a.Units(), a.val()+b.val());
}
const PredictionValue operator- (PredictionValue a, PredictionValue b) {
  assert (a.Units() == b.Units());
  return PredictionValue (a.Units(), a.val()-b.val());
}
const double operator/ (PredictionValue a, PredictionValue b) {
  assert (a.Units() == b.Units());
  return a.val()/b.val();
}


const PredictionValue operator/ (PredictionValue b, double a) {
  b /= a;
  return b;
}


static void harmonize (PredictionValue &a, PredictionValue &b) {
  if (a.Units() != b.Units()) {
    if (a.Units() == Units::zulu)
      a.Units (b.Units());
    else
      b.Units (a.Units());
  }
}


const bool operator> (PredictionValue a, PredictionValue b) {
  harmonize (a, b);
  return (a.val() > b.val());
}


const bool operator< (PredictionValue a, PredictionValue b) {
  harmonize (a, b);
  return (a.val() < b.val());
}


const bool operator<= (PredictionValue a, PredictionValue b) {
  harmonize (a, b);
  return (a.val() <= b.val());
}


const bool operator>= (PredictionValue a, PredictionValue b) {
  harmonize (a, b);
  return (a.val() >= b.val());
}


const bool operator== (PredictionValue a, PredictionValue b) {
  harmonize (a, b);
  return (a.val() == b.val());
}


const bool operator!= (PredictionValue a, PredictionValue b) {
  harmonize (a, b);
  return (a.val() != b.val());
}


const PredictionValue operator- (PredictionValue a) {
  return PredictionValue (a.Units(), -a.val());
}


const PredictionValue abs (PredictionValue a) {
  return PredictionValue (a.Units(), fabs(a.val()));
}


// Print in the form -XX.YY units (padding as needed)
void PredictionValue::print (Dstr &text_out) const {
  char temp[80];
  if (Global::settings["ou"].c == 'y')
    sprintf (temp, "% 6.2f", _value);
  else
    sprintf (temp, "% 6.2f %s", _value, Units::longName(_units));
  text_out = temp;
}


void PredictionValue::printnp (Dstr &text_out) const {
  char temp[80];
  if (Global::settings["ou"].c == 'y')
    sprintf (temp, "%2.2f", _value);
  else
    sprintf (temp, "%2.2f %s", _value, Units::shortName(_units));
  text_out = temp;
}

}
