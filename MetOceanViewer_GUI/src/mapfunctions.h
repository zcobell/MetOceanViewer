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
#ifndef MAPFUNCTIONS_H
#define MAPFUNCTIONS_H

#include <QComboBox>
#include <QObject>
#include "station.h"
#include "stationmodel.h"

class MapFunctions : public QObject {
  Q_OBJECT
 public:
  enum MarkerType { NOAA, USGS, XTIDE };

  explicit MapFunctions(QObject *parent = nullptr);

  static QVector<Station> readMarkers(MapFunctions::MarkerType markerType);

  static int refreshMarkers(StationModel *model, QQuickWidget *map,
                            QVector<Station> &locations);

  static void setEsriMapTypes(QComboBox *comboBox);
};

#endif  // MAPFUNCTIONS_H