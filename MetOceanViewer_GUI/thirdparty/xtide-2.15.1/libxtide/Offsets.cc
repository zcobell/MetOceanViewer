// $Id: Offsets.cc 5748 2014-10-11 19:38:53Z flaterco $

// Offsets:  storage for tide offsets.

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

#include "libxtide.hh"
namespace libxtide {


SimpleOffsets::SimpleOffsets (Interval timeAdd,
			      PredictionValue levelAdd,
                              double levelMultiply):
  _timeAdd(timeAdd),
  _levelAdd(levelAdd),
  _levelMultiply(levelMultiply) {
  if (_levelMultiply == 0.0)
    _levelMultiply = 1.0;
  assert (_levelMultiply > 0.0);
}


SimpleOffsets::SimpleOffsets():
  _levelMultiply(1.0) {}


const Interval SimpleOffsets::timeAdd() const {
  return _timeAdd;
}


const PredictionValue SimpleOffsets::levelAdd() const {
  return _levelAdd;
}


const double SimpleOffsets::levelMultiply () const {
  return _levelMultiply;
}


const bool operator== (const SimpleOffsets &a, const SimpleOffsets &b) {
  return (a.timeAdd()       == b.timeAdd()       &&
          a.levelMultiply() == b.levelMultiply() &&
	  a.levelAdd()      == b.levelAdd());
}


const bool operator!= (const SimpleOffsets &a, const SimpleOffsets &b) {
  return !(a == b);
}


HairyOffsets::HairyOffsets (const SimpleOffsets &maxso,
                            const SimpleOffsets &minso,
                            NullableInterval floodBegins,
			    NullableInterval ebbBegins):
  _maxso(maxso),
  _minso(minso),
  _floodBegins(floodBegins),
  _ebbBegins(ebbBegins) {}


const Interval         HairyOffsets::maxTimeAdd()       const {
  return _maxso.timeAdd();
}


const PredictionValue  HairyOffsets::maxLevelAdd()      const {
  return _maxso.levelAdd();
}


const double           HairyOffsets::maxLevelMultiply() const {
  return _maxso.levelMultiply();
}


const Interval         HairyOffsets::minTimeAdd()       const {
  return _minso.timeAdd();
}


const PredictionValue  HairyOffsets::minLevelAdd()      const {
  return _minso.levelAdd();
}


const double           HairyOffsets::minLevelMultiply() const {
  return _minso.levelMultiply();
}


const NullableInterval HairyOffsets::floodBegins()      const {
  return _floodBegins;
}


const NullableInterval HairyOffsets::ebbBegins()        const {
  return _ebbBegins;
}


const bool HairyOffsets::trySimplify (SimpleOffsets &simpleOffsets_out) const {
  if (_maxso != _minso)
    return false;
  // equal levelAdds cause the slacks to shift unless they are 0.
  // equal levelMults do not move the slacks, even if there's a permanent
  // current.  (The distance between the middle level and the zero level
  // is stretched proportional to the multiplier, so the time at which
  // the slack occurs is unchanged.)
  // See also special case for hydraulics in HarmonicsFile.cc.
  if (!_floodBegins.isNull())
    if (_floodBegins != _maxso.timeAdd() || _maxso.levelAdd().val() != 0.0)
      return false;
  if (!_ebbBegins.isNull())
    if (_ebbBegins != _maxso.timeAdd() || _maxso.levelAdd().val() != 0.0)
      return false;
  simpleOffsets_out = _maxso;
  return true;
}

}
