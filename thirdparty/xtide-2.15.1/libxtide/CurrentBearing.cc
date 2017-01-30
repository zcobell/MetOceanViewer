// $Id: CurrentBearing.cc 5748 2014-10-11 19:38:53Z flaterco $

/*
    CurrentBearing:  Store and print <int>° or <int>° true, or null.

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


CurrentBearing::CurrentBearing () {}


CurrentBearing::CurrentBearing (uint16_t degrees, bool isTrue):
  Nullable(false),
  _degrees(degrees),
  isDegreesTrue(isTrue) {
  assert (_degrees < 360);
}


void CurrentBearing::print (Dstr &text_out) const {
  assert (!_isNull);
  text_out = _degrees;
  text_out += '°';
  if (isDegreesTrue)
    text_out += " true";
}

}
