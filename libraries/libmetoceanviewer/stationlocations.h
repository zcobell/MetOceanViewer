/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2018  Zach Cobell
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

#include <QObject>
#include <QVector>
#include "metoceanviewer_global.h"
#include "station.h"

class METOCEANSHARED_EXPORT StationLocations : public QObject {
  Q_OBJECT
 public:
  explicit StationLocations(QObject *parent = nullptr);

  enum MarkerType { NOAA, USGS, XTIDE, NDBC };

  static QVector<Station> readMarkers(MarkerType markerType);

 private:
  static QVector<Station> readNoaaMarkers();
  static QVector<Station> readUsgsMarkers();
  static QVector<Station> readXtideMarkers();
  static QVector<Station> readNdbcMarkers();
};

#endif  // STATIONLOCATIONS_H
