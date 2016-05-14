// $Id: NullableInterval.cc 5748 2014-10-11 19:38:53Z flaterco $

// NullableInterval:  strict nullable Interval.

/*
    Copyright (C) 2004  David Flater.

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


NullableInterval::NullableInterval() {}


NullableInterval::NullableInterval (const Interval &interval):
  Nullable(false),
  _interval(interval) {}


const interval_rep_t NullableInterval::s () const {
  assert (!_isNull);
  return _interval.s();
}

}
