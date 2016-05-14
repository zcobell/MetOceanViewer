// $Id: NullablePredictionValue.cc 5748 2014-10-11 19:38:53Z flaterco $

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

#include "libxtide.hh"
namespace libxtide {


NullablePredictionValue::NullablePredictionValue() {}


NullablePredictionValue::NullablePredictionValue (PredictionValue pv):
  Nullable(false),
  _pv(pv) {}


NullablePredictionValue::NullablePredictionValue (
              Units::PredictionUnits units,
              double value):
  Nullable(false),
  _pv(units, value) {}


const Units::PredictionUnits NullablePredictionValue::Units () const {
  assert (!_isNull);
  return _pv.Units();
}


const double NullablePredictionValue::val() const {
  assert (!_isNull);
  return _pv.val();
}


void NullablePredictionValue::Units (Units::PredictionUnits units) {
  assert (!_isNull);
  _pv.Units (units);
}


void NullablePredictionValue::operator*= (double multiplier) {
  assert (!_isNull);
  _pv *= multiplier;
}


void NullablePredictionValue::operator/= (double divisor) {
  assert (!_isNull);
  _pv /= divisor;
}


void NullablePredictionValue::convertAndAdd (PredictionValue addend) {
  assert (!_isNull);
  _pv.convertAndAdd (addend);
}


void NullablePredictionValue::print (Dstr &text_out) const {
  assert (!_isNull);
  _pv.print (text_out);
}


void NullablePredictionValue::printnp (Dstr &text_out) const {
  assert (!_isNull);
  _pv.printnp (text_out);
}

}
