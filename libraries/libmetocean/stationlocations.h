/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2019  Zach Cobell
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//-----------------------------------------------------------------------*/
#ifndef STATIONLOCATIONS_H
#define STATIONLOCATIONS_H

#include <vector>

#include "metocean_global.h"
#include "movStation.h"

class StationLocations {
public:
  enum MarkerType { NOAA, USGS, XTIDE, NDBC, CRMS };

  static METOCEANSHARED_EXPORT std::vector<MovStation>
  readMarkers(MarkerType markerType);

private:
  static std::vector<MovStation> readNoaaMarkers();
  static std::vector<MovStation> readUsgsMarkers();
  static std::vector<MovStation> readXtideMarkers();
  static std::vector<MovStation> readNdbcMarkers();
  static std::vector<MovStation> readCrmsMarkers();
};

#endif // STATIONLOCATIONS_H
