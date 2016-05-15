// $Id: StationRef.hh 5748 2014-10-11 19:38:53Z flaterco $

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

namespace libxtide {

class Station;

class StationRef {
public:

  // harmonicsFileName + recordNumber uniquely identify a station.
  StationRef (const Dstr &harmonicsFileName_,
              uint32_t recordNumber_,
              const Dstr &name_,
              const Coordinates &coordinates_,
              const Dstr &timezone_,
              bool isReferenceStation_,
              bool isCurrent_);

  const Dstr &       harmonicsFileName;
  const uint32_t     recordNumber;
  const Dstr         name;
  const Coordinates  coordinates;
  const Dstr         timezone;
  const bool         isReferenceStation;
  const bool         isCurrent;

  // This is the index of this StationRef in Global::stationIndex().
  // It is used only for xttpd to create hyperlinks back to stations.
  // It is set in StationIndex::setRootStationIndexIndices() (invoked
  // by Global::stationIndex()) and referenced in
  // StationIndex::listLocationHTML() (invoked by xttpd).
  unsigned long rootStationIndexIndex;

  // Load the referenced station.
  Station * const load() const;
};

// Comparison functions for use with std::sort.
const bool sortByName (const StationRef *x, const StationRef *y);
const bool sortByLat  (const StationRef *x, const StationRef *y);
const bool sortByLng  (const StationRef *x, const StationRef *y);

}
