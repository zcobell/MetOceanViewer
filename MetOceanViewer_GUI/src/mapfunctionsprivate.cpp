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
#include "mapfunctionsprivate.h"
#include <QFile>

MapFunctionsPrivate::MapFunctionsPrivate(QObject *parent) : QObject(parent) {}

QVector<Station> MapFunctionsPrivate::readNoaaMarkers() {
  QVector<Station> output;

  QFile stationFile(":/stations/data/noaa_stations.csv");

  if (!stationFile.open(QIODevice::ReadOnly)) return output;

  int index = 0;

  while (!stationFile.atEnd()) {
    QString line = stationFile.readLine().simplified();
    index++;
    if (index > 1) {
      QStringList list = line.split(";");
      QString id = list.value(0);
      QString name = list.value(3);
      name = name.simplified();
      QString temp = list.value(1);
      double lat = temp.toDouble();
      temp = list.value(2);
      double lon = temp.toDouble();
      output.push_back(Station(QGeoCoordinate(lat, lon), id, name));
    }
  }

  stationFile.close();

  return output;
}

QVector<Station> MapFunctionsPrivate::readUsgsMarkers() {
  QVector<Station> output;

  QFile stationFile(":/stations/data/usgs_stations.csv");

  if (!stationFile.open(QIODevice::ReadOnly)) return output;

  int index = 0;

  while (!stationFile.atEnd()) {
    QString line = stationFile.readLine().simplified();
    index++;
    if (index > 1) {
      QStringList list = line.split(";");
      QString id = list.value(0);
      QString name = list.value(1);
      name = name.simplified();
      QString temp = list.value(2);
      double lat = temp.toDouble();
      temp = list.value(3);
      double lon = temp.toDouble();
      output.push_back(Station(QGeoCoordinate(lat, lon), id, name));
    }
  }

  stationFile.close();

  return output;
}

QVector<Station> MapFunctionsPrivate::readXtideMarkers() {
  QVector<Station> output;
  QFile stationFile(":/stations/data/xtide_stations.csv");

  if (!stationFile.open(QIODevice::ReadOnly)) return output;

  int index = 0;

  while (!stationFile.atEnd()) {
    QString line = stationFile.readLine().simplified();
    index++;
    if (index > 1) {
      QStringList list = line.split(";");
      QString id = list.value(3);
      QString name = list.value(4);
      name = name.simplified();
      QString temp = list.value(0);
      double lat = temp.toDouble();
      temp = list.value(1);
      double lon = temp.toDouble();
      output.push_back(Station(QGeoCoordinate(lat, lon), id, name));
    }
  }

  stationFile.close();

  return output;
}
