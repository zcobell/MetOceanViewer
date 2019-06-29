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
#include "stationlocations.h"
#include <QFile>
#include "generic.h"

StationLocations::StationLocations(QObject *parent) : QObject(parent) {}

QVector<Station> StationLocations::readMarkers(
    StationLocations::MarkerType markerType) {
  if (markerType == NOAA) {
    return StationLocations::readNoaaMarkers();
  } else if (markerType == USGS) {
    return StationLocations::readUsgsMarkers();
  } else if (markerType == XTIDE) {
    return StationLocations::readXtideMarkers();
  } else if (markerType == NDBC) {
    return StationLocations::readNdbcMarkers();
  } else if (markerType == CRMS) {
    return StationLocations::readCrmsMarkers();
  } else {
    QVector<Station> output;
    return output;
  }
}

QVector<Station> StationLocations::readNoaaMarkers() {
  QVector<Station> output;

  QFile stationFile(":/stations/data/noaa_stations.csv");

  if (!stationFile.open(QIODevice::ReadOnly)) return output;

  while (!stationFile.atEnd()) {
    QString line = stationFile.readLine().simplified();
    QStringList list = line.split(";");
    QString id = list.value(0);
    QString name = list.value(1);
    name = name.simplified();
    QString temp = list.value(3);
    double lat = temp.toDouble();
    temp = list.value(2);
    double lon = temp.toDouble();

    QString startDateString = list.value(4).simplified();
    QString endDateString = list.value(5).simplified();
    QDateTime startDate =
        QDateTime::fromString(startDateString, "MMM dd, yyyy");
    startDate.setTimeSpec(Qt::UTC);
    QDateTime endDate;
    if (endDateString == "present")
      endDate = QDateTime(QDate(2050, 1, 1), QTime(0, 0, 0));
    else
      endDate = QDateTime::fromString(endDateString, "MMM dd, yyyy");
    endDate.setTimeSpec(Qt::UTC);

    if (startDate.isValid() || endDate.isValid()) {
      if (endDateString == "present") {
        Station s = Station(QGeoCoordinate(lat, lon), id, name, 0, 0, 0, true,
                            startDate, endDate);
        output.push_back(s);
      } else {
        Station s = Station(QGeoCoordinate(lat, lon), id, name, 0, 0, 0, false,
                            startDate, endDate);
        output.push_back(s);
      }
    }
  }

  stationFile.close();

  return output;
}

QVector<Station> StationLocations::readUsgsMarkers() {
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
      Station s = Station(QGeoCoordinate(lat, lon), id, name);
      output.push_back(s);
    }
  }

  stationFile.close();

  return output;
}

QVector<Station> StationLocations::readXtideMarkers() {
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
      Station s = Station(QGeoCoordinate(lat, lon), id, name);
      output.push_back(s);
    }
  }

  stationFile.close();

  return output;
}

QVector<Station> StationLocations::readNdbcMarkers() {
  QVector<Station> output;
  QFile stationFile(":/stations/data/ndbc_stations.csv");

  if (!stationFile.open(QIODevice::ReadOnly)) return output;

  int index = 0;

  while (!stationFile.atEnd()) {
    QString line = stationFile.readLine().simplified();
    index++;
    if (index > 1) {
      QStringList list = line.split(",");
      QString id = list.value(0).simplified();
      QString name = "NDBC_" + id;
      QString temp = list.value(1);
      double lon = temp.toDouble();
      temp = list.value(2);
      double lat = temp.toDouble();
      Station s = Station(QGeoCoordinate(lat, lon), id, name);
      output.push_back(s);
    }
  }

  stationFile.close();

  return output;
}

QVector<Station> StationLocations::readCrmsMarkers() {
  QVector<Station> output;
  QVector<double> latitude, longitude;
  QVector<QString> name;
  QVector<QDateTime> startDate, endDate;
  QString filename = Generic::crmsDataFile();
  bool success = CrmsData::readStationList(filename, latitude, longitude, name,
                                           startDate, endDate);

  if (!success) return output;

  for (size_t i = 0; i < latitude.size(); ++i) {
    QGeoCoordinate c(latitude[i], longitude[i]);
    QString id;
    id.sprintf("%zu", i);
    Station s(c, id, name[i], 0, 0, 0, true, startDate[i], endDate[i]);
    output.push_back(s);
  }

  return output;
}
