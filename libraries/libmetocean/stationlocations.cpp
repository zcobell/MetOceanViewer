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
#include "stationlocations.h"

#include <QFile>

#include "boost/format.hpp"
#include "crmsdata.h"
#include "generic.h"
#include "stringutil.h"

std::vector<MovStation>
StationLocations::readMarkers(StationLocations::MarkerType markerType) {
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
    return std::vector<MovStation>();
  }
}

std::vector<MovStation> StationLocations::readNoaaMarkers() {
  QFile stationFile(":/stations/data/noaa_stations.csv");

  if (!stationFile.open(QIODevice::ReadOnly))
    return std::vector<MovStation>();

  std::vector<MovStation> output;
  while (!stationFile.atEnd()) {
    std::string line = stationFile.readLine().simplified().toStdString();
    std::vector<std::string> list =
        StringUtil::stringSplitToVector(line, ";", false);
    std::string id = list[0];
    std::string name = list[1];
    name = StringUtil::sanitizeString(name);
    double lat = stod(list[3]);
    double lon = stod(list[2]);

    QString startDateString =
        QString::fromStdString(StringUtil::sanitizeString(list[4]));
    QString endDateString =
        QString::fromStdString(StringUtil::sanitizeString(list[5]));
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
      MovStation s = MovStation();
      if (endDateString == "present") {
        s = MovStation(QGeoCoordinate(lat, lon), QString::fromStdString(id),
                       QString::fromStdString(name), 0, 0, 0, true, startDate,
                       endDate);
      } else {
        s = MovStation(QGeoCoordinate(lat, lon), QString::fromStdString(id),
                       QString::fromStdString(name), 0, 0, 0, false, startDate,
                       endDate);
      }
      double mllw = stod(list[6]);
      double mlw = stod(list[7]);
      double mhw = stod(list[9]);
      double mhhw = stod(list[10]);
      double ngvd = stod(list[11]);
      double navd = stod(list[12]);

      if (mlw < -900.0)
        mlw = MovStation::nullOffset();
      if (mllw < -900.0)
        mllw = MovStation::nullOffset();
      if (mhw < -900.0)
        mhw = MovStation::nullOffset();
      if (mhhw < -900.0)
        mhhw = MovStation::nullOffset();
      if (ngvd < -900.0)
        ngvd = MovStation::nullOffset();
      if (navd < -900.0)
        navd = MovStation::nullOffset();

      s.setMllwOffset(mllw);
      s.setMlwOffset(mlw);
      s.setMslOffset(0.0);
      s.setMhwOffset(mhw);
      s.setMhhwOffset(mhhw);
      s.setNgvd29Offset(ngvd);
      s.setNavd88Offset(navd);

      output.push_back(s);
    }
  }

  stationFile.close();

  return output;
}

std::vector<MovStation> StationLocations::readUsgsMarkers() {
  QFile stationFile(":/stations/data/usgs_stations.csv");

  if (!stationFile.open(QIODevice::ReadOnly))
    return std::vector<MovStation>();

  int index = 0;

  std::vector<MovStation> output;
  while (!stationFile.atEnd()) {
    std::string line = stationFile.readLine().simplified().toStdString();
    index++;
    if (index > 1) {
      std::vector<std::string> list =
          StringUtil::stringSplitToVector(line, ";");
      std::string id = list[0];
      std::string name = StringUtil::sanitizeString(list[1]);
      double lat = stod(list[2]);
      double lon = stod(list[3]);
      MovStation s =
          MovStation(QGeoCoordinate(lat, lon), QString::fromStdString(id),
                     QString::fromStdString(name));
      output.push_back(s);
    }
  }

  stationFile.close();

  return output;
}

std::vector<MovStation> StationLocations::readXtideMarkers() {
  QFile stationFile(":/stations/data/xtide_stations.csv");

  if (!stationFile.open(QIODevice::ReadOnly))
    return std::vector<MovStation>();

  int index = 0;

  std::vector<MovStation> output;
  while (!stationFile.atEnd()) {
    std::string line = stationFile.readLine().simplified().toStdString();
    index++;
    if (index > 1) {
      std::vector<std::string> list =
          StringUtil::stringSplitToVector(line, ";");
      std::string id = list[3];
      std::string name = list[4];
      name = StringUtil::sanitizeString(name);
      double lat = stod(list[0]);
      double lon = stod(list[1]);
      MovStation s =
          MovStation(QGeoCoordinate(lat, lon), QString::fromStdString(id),
                     QString::fromStdString(name));

      double mlw = stod(list[6]);
      double msl = stod(list[7]);
      double mhw = stod(list[8]);
      double mhhw = stod(list[9]);
      double ngvd = stod(list[10]);
      double navd = stod(list[11]);

      if (mlw < -900.0)
        mlw = MovStation::nullOffset();
      if (msl < -900.0)
        msl = MovStation::nullOffset();
      if (mhw < -900.0)
        mhw = MovStation::nullOffset();
      if (mhhw < -900.0)
        mhhw = MovStation::nullOffset();
      if (ngvd < -900.0)
        ngvd = MovStation::nullOffset();
      if (navd < -900.0)
        navd = MovStation::nullOffset();

      s.setMllwOffset(0.0);
      s.setMlwOffset(mlw);
      s.setMslOffset(msl);
      s.setMhwOffset(mhw);
      s.setMhhwOffset(mhhw);
      s.setNgvd29Offset(ngvd);
      s.setNavd88Offset(navd);

      output.push_back(s);
    }
  }

  stationFile.close();

  return output;
}

std::vector<MovStation> StationLocations::readNdbcMarkers() {
  QFile stationFile(":/stations/data/ndbc_stations.csv");

  if (!stationFile.open(QIODevice::ReadOnly))
    return std::vector<MovStation>();

  int index = 0;

  std::vector<MovStation> output;
  while (!stationFile.atEnd()) {
    std::string line = stationFile.readLine().simplified().toStdString();
    index++;
    if (index > 1) {
      std::vector<std::string> list =
          StringUtil::stringSplitToVector(line, ",");
      std::string id = list[0];
      std::string name = "NDBC_" + id;
      double lon = stod(list[1]);
      double lat = stod(list[2]);
      MovStation s =
          MovStation(QGeoCoordinate(lat, lon), QString::fromStdString(id),
                     QString::fromStdString(name));
      output.push_back(s);
    }
  }

  stationFile.close();

  return output;
}

std::vector<MovStation> StationLocations::readCrmsMarkers() {
  std::vector<MovStation> output;
  std::vector<double> latitude, longitude;
  std::vector<std::string> name;
  std::vector<QDateTime> startDate, endDate;
  std::string filename = Generic::crmsDataFile();
  bool success = CrmsData::readStationList(filename, latitude, longitude, name,
                                           startDate, endDate);

  if (!success)
    return output;

  for (size_t i = 0; i < latitude.size(); ++i) {
    QGeoCoordinate c(latitude[i], longitude[i]);
    std::string id = boost::str(boost::format("%llu") % i);
    MovStation s(c, QString::fromStdString(id), QString::fromStdString(name[i]),
                 0, 0, 0, true, startDate[i], endDate[i]);
    output.push_back(s);
  }

  return output;
}
