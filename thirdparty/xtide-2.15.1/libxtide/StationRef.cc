// $Id: StationRef.cc 5748 2014-10-11 19:38:53Z flaterco $

/*  StationRef  Index information for a station in a harmonics file.

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
#include "HarmonicsFile.hh"

namespace libxtide {


Station * const StationRef::load() const {
  HarmonicsFile h (harmonicsFileName);
  return h.getStation (*this);
}


StationRef::StationRef (const Dstr &harmonicsFileName_,
                        uint32_t recordNumber_,
                        const Dstr &name_,
                        const Coordinates &coordinates_,
                        const Dstr &timezone_,
                        bool isReferenceStation_,
                        bool isCurrent_):
  harmonicsFileName(harmonicsFileName_),
  recordNumber(recordNumber_),
  name(name_),
  coordinates(coordinates_),
  timezone(timezone_),
  isReferenceStation(isReferenceStation_),
  isCurrent(isCurrent_) {}


const bool sortByName (const StationRef *x, const StationRef *y) {
  return (dstrcasecmp (x->name, y->name) < 0);
}


const bool sortByLat (const StationRef *x, const StationRef *y) {
  const Coordinates &left = x->coordinates;
  const Coordinates &right = y->coordinates;
  if (left.isNull())
    return (!(right.isNull()));
  if (right.isNull())
    return false;
  return (left.lat() < right.lat());
}


const bool sortByLng (const StationRef *x, const StationRef *y) {
  const Coordinates &left = x->coordinates;
  const Coordinates &right = y->coordinates;
  if (left.isNull())
    return (!(right.isNull()));
  if (right.isNull())
    return false;
  return (left.lng() < right.lng());
}

}
