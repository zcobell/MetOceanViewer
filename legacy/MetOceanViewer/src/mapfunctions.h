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
#ifndef MAPFUNCTIONS_H
#define MAPFUNCTIONS_H

#include <QComboBox>
#include <QObject>
#include <memory>
#include "station.h"
#include "stationmodel.h"

class MapFunctions : public QObject {
  Q_OBJECT
 public:
  enum MapSource { ESRI, MapBox, OSM };

  explicit MapFunctions(QObject *parent = nullptr);

  int refreshMarkers(StationModel *model, QQuickWidget *map,
                     QVector<Station> &locations, bool filter = true,
                     bool activeOnly = true);

  int refreshMarkers(StationModel *model, QQuickWidget *map,
                     QVector<Station> &locations, QDateTime &start,
                     QDateTime &end);

  void setMapTypes(QQuickWidget *map, QComboBox *comboBox);

  int mapSource() const;
  void setMapSource(int mapSource);

  void setMapQmlFile(QQuickWidget *map);

  QString mapboxApiKey() const;
  void setMapboxApiKey(const QString &mapboxApiKey);

  void getMapboxKeyFromDisk();
  void saveMapboxKeyToDisk();

  void getConfigurationFromDisk();
  void saveConfigurationToDisk();

  int getDefaultMapIndex() const;
  void setDefaultMapIndex(int defaultMapIndex);

  void setMapType(int index, QQuickWidget *map);

 private:
  int m_mapSource;
  int m_defaultMapIndex;
  QString m_configDirectory;
  QString m_mapboxApiKey;
};

#endif  // MAPFUNCTIONS_H
