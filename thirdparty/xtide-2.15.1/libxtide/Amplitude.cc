/*  $Id: Amplitude.cc 5748 2014-10-11 19:38:53Z flaterco $

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

#include "libxtide.hh"
namespace libxtide {


Amplitude::Amplitude () {}


Amplitude::Amplitude (Units::PredictionUnits units,
                      double value):
  pv(units,value) {
  assert (value >= 0.0);
}


void Amplitude::Units (Units::PredictionUnits units) {
  pv.Units (units);
}


void Amplitude::operator*= (double levelMultiply) {
  assert (levelMultiply >= 0.0);
  pv *= levelMultiply;
}


void Amplitude::operator+= (Amplitude argument) {
  pv += argument.pv;
}


const Amplitude operator* (double a, Amplitude b) {
  b *= a;
  return b;
}


const Amplitude operator* (Amplitude b, double a) {
  b *= a;
  return b;
}


const bool operator> (Amplitude a, Amplitude b) {
  return (a.pv > b.pv);
}


const bool operator< (Amplitude a, Amplitude b) {
  return (a.pv < b.pv);
}

}
