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
#include "adcircstationoutput.h"
#include <QFile>
#include <algorithm>
#include <cmath>
#include <iterator>
#include "errors.h"
#include "hmdf.h"
#include "netcdf.h"

AdcircStationOutput::AdcircStationOutput(QObject *parent) : QObject(parent) {
  this->_error = MetOceanViewer::Error::NOERR;
  this->_ncerr = NC_NOERR;
  this->nStations = 0;
  this->nSnaps = 0;
}

int AdcircStationOutput::error() { return this->_error; }

QString AdcircStationOutput::errorString() { return "errorString"; }

int AdcircStationOutput::read(QString AdcircFile, QString AdcircStationFile,
                              QDateTime coldStart) {
  this->coldStartTime = coldStart;
  this->_error = this->readAscii(AdcircFile, AdcircStationFile);
  return this->_error;
}

int AdcircStationOutput::read(QString AdcircFile, QDateTime coldStart) {
  this->coldStartTime = coldStart;
  this->_error = this->readNetCDF(AdcircFile);
  return this->_error;
}

int AdcircStationOutput::readAscii(QString AdcircOutputFile,
                                   QString AdcircStationFile) {
  QFile MyFile(AdcircOutputFile), StationFile(AdcircStationFile);
  QString header1, header2, TempLine;
  QStringList headerData, TempList;
  int nColumns;

  // Check if we can open the file
  if (!MyFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    this->_error = MetOceanViewer::Error::CANNOT_OPEN_FILE;
    return this->_error;
  }

  if (!StationFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    this->_error = MetOceanViewer::Error::CANNOT_OPEN_FILE;
    return this->_error;
  }

  // Read the 61/62 style file
  header1 = MyFile.readLine();
  header2 = MyFile.readLine().simplified();
  headerData = header2.split(" ");

  this->nSnaps = headerData.value(0).toInt();
  this->nStations = headerData.value(1).toInt();
  nColumns = headerData.value(4).toInt();

  this->time.resize(this->nSnaps);
  this->data.resize(this->nStations);

  for (size_t i = 0; i < this->nStations; ++i)
    this->data[i].resize(this->nSnaps);

  for (size_t i = 0; i < this->nSnaps; ++i) {
    TempLine = MyFile.readLine().simplified();
    TempList = TempLine.split(" ");
    this->time[i] = TempList.value(0).toDouble();
    for (size_t j = 0; j < this->nStations; ++j) {
      TempLine = MyFile.readLine().simplified();
      TempList = TempLine.split(" ");
      if (TempList.value(1).toDouble() < -900) {
        this->data[j][i] = HmdfStation::nullDataValue();
      } else {
        this->data[j][i] = TempList.value(1).toDouble();
        if (nColumns == 2)
          this->data[j][i] = pow(
              pow(this->data[j][i], 2) + pow(TempList.value(2).toDouble(), 2),
              2);
      }
    }
  }
  MyFile.close();

  // Now read the station location file
  TempLine = StationFile.readLine().simplified();
  TempList = TempLine.split(" ");
  size_t TempStations = TempList.value(0).toULongLong();
  if (TempStations != this->nStations)
    return MetOceanViewer::Error::WRONG_NUMBER_OF_STATIONS;

  this->longitude.resize(this->nStations);
  this->latitude.resize(this->nStations);
  this->station_name.resize(this->nStations);

  for (size_t i = 0; i < TempStations; ++i) {
    TempLine = StationFile.readLine().simplified();
    TempList = TempLine.split(QRegExp(",| "));
    this->longitude[i] = TempList.value(0).toDouble();
    this->latitude[i] = TempList.value(1).toDouble();

    if (TempList.length() > 2) {
      this->station_name[i] = "";
      for (int j = 2; j < TempList.length(); ++j)
        this->station_name[i] = this->station_name[i] + " " + TempList.value(j);
    } else
      this->station_name[i] = tr("Station_") + QString::number(i);
  }
  StationFile.close();

  return MetOceanViewer::Error::NOERR;
}

int AdcircStationOutput::readNetCDF(QString AdcircOutputFile) {
  size_t station_size, time_size;
  int ncid, varid_zeta, varid_zeta2, varid_lat, varid_lon, varid_time;
  int dimid_time, dimid_station;
  bool isVector;

  // Size the location array
  size_t start[2];
  size_t count[2];

  QVector<QString> netcdf_types;
  netcdf_types.resize(6);
  netcdf_types[0] = "zeta";
  netcdf_types[1] = "u-vel";
  netcdf_types[2] = "v-vel";
  netcdf_types[3] = "pressure";
  netcdf_types[4] = "windx";
  netcdf_types[5] = "windy";

  // Open the file
  this->_ncerr = nc_open(AdcircOutputFile.toUtf8(), NC_NOWRITE, &ncid);
  if (this->_ncerr != NC_NOERR) {
    this->_error = MetOceanViewer::Error::NETCDF;
    return this->_error;
  }

  // Get the dimension ids
  this->_ncerr = nc_inq_dimid(ncid, "time", &dimid_time);
  if (this->_ncerr != NC_NOERR) {
    this->_error = MetOceanViewer::Error::NETCDF;
    return this->_error;
  }

  this->_ncerr = nc_inq_dimid(ncid, "station", &dimid_station);
  if (this->_ncerr != NC_NOERR) {
    this->_error = MetOceanViewer::Error::NETCDF;
    return this->_error;
  }

  // Find out the dimension size
  this->_ncerr = nc_inq_dimlen(ncid, dimid_time, &time_size);
  if (this->_ncerr != NC_NOERR) {
    this->_error = MetOceanViewer::Error::NETCDF;
    return this->_error;
  }

  this->_ncerr = nc_inq_dimlen(ncid, dimid_station, &station_size);
  if (this->_ncerr != NC_NOERR) {
    this->_error = MetOceanViewer::Error::NETCDF;
    return this->_error;
  }

  // Find the variable in the NetCDF file
  for (size_t i = 0; i < 6; i++) {
    int ierr = nc_inq_varid(ncid, netcdf_types[i].toUtf8(), &varid_zeta);

    // If we found the variable, we're done
    if (ierr == NC_NOERR) {
      if (i == 1 || i == 4) {
        isVector = true;
        this->_ncerr =
            nc_inq_varid(ncid, netcdf_types[i + 1].toUtf8(), &varid_zeta2);
        if (this->_ncerr != NC_NOERR) {
          this->_error = MetOceanViewer::Error::NETCDF;
          return this->_error;
        }
      } else
        isVector = false;

      break;
    }

    // If we're at the end of the array
    // and haven't quit yet, that's a problem
    if (i == 5) return MetOceanViewer::Error::NO_VARIABLE_FOUND;
  }

  // Size the output variables
  this->nStations = station_size;
  this->nSnaps = time_size;
  this->data.resize(station_size);
  for (size_t i = 0; i < station_size; ++i) this->data[i].resize(time_size);

  // Read the station locations and times
  this->_ncerr = nc_inq_varid(ncid, "time", &varid_time);
  if (this->_ncerr != NC_NOERR) {
    this->_error = MetOceanViewer::Error::NETCDF;
    return this->_error;
  }

  this->_ncerr = nc_inq_varid(ncid, "x", &varid_lon);
  if (this->_ncerr != NC_NOERR) {
    this->_error = MetOceanViewer::Error::NETCDF;
    return this->_error;
  }

  this->_ncerr = nc_inq_varid(ncid, "y", &varid_lat);
  if (this->_ncerr != NC_NOERR) {
    this->_error = MetOceanViewer::Error::NETCDF;
    return this->_error;
  }

  double fillVal;
  this->_ncerr = nc_inq_var_fill(ncid, varid_zeta, NULL, &fillVal);
  if (this->_ncerr != NC_NOERR) {
    this->_error = MetOceanViewer::Error::NETCDF;
    return this->_error;
  }

  this->time.resize(time_size);
  this->_ncerr = nc_get_var_double(ncid, varid_time, this->time.data());
  if (this->_ncerr != NC_NOERR) {
    this->_error = MetOceanViewer::Error::NETCDF;
    return this->_error;
  }

  this->longitude.resize(station_size);
  this->_ncerr = nc_get_var_double(ncid, varid_lon, this->longitude.data());
  if (this->_ncerr != NC_NOERR) {
    this->_error = MetOceanViewer::Error::NETCDF;
    return this->_error;
  }

  this->latitude.resize(station_size);
  this->_ncerr = nc_get_var_double(ncid, varid_lat, this->latitude.data());
  if (this->_ncerr != NC_NOERR) {
    this->_error = MetOceanViewer::Error::NETCDF;
    return this->_error;
  }

  // Loop over the stations, reading the data into memory
  for (size_t i = 0; i < station_size; ++i) {
    std::vector<double> tempVar1(time_size);
    std::vector<double> tempVar2(time_size);

    // Read from netCDF
    start[0] = 0;
    start[1] = i;
    count[0] = time_size;
    count[1] = 1;
    this->_ncerr = nc_get_vara(ncid, varid_zeta, start, count, tempVar1.data());
    if (this->_ncerr != NC_NOERR) {
      this->_error = MetOceanViewer::Error::NETCDF;
      return this->_error;
    }

    if (isVector) {
      this->_ncerr =
          nc_get_vara(ncid, varid_zeta2, start, count, tempVar2.data());
      if (this->_ncerr != NC_NOERR) {
        this->_error = MetOceanViewer::Error::NETCDF;
        return this->_error;
      }
      for (size_t j = 0; j < time_size; j++) {
        if (tempVar1[j] != fillVal) {
          this->data[i][j] =
              sqrt(pow(tempVar1[j], 2.0) + pow(tempVar2[j], 2.0));
        } else {
          this->data[i][j] = HmdfStation::nullDataValue();
        }
      }
    } else {
      for (size_t j = 0; j < time_size; ++j) {
        if (tempVar1[j] != fillVal) {
          this->data[i][j] = tempVar1[j];
        } else {
          this->data[i][j] = HmdfStation::nullDataValue();
        }
      }
    }
  }
  this->_error = nc_close(ncid);
  if (this->_error != NC_NOERR) {
    this->_ncerr = this->_error;
    this->_error = MetOceanViewer::Error::NETCDF;
    return this->_error;
  }

  // Finally, name the stations the default names for now. Later
  // we can get fancy and try to get the ADCIRC written names in
  // the NetCDF file
  this->station_name.resize(station_size);
  for (size_t i = 0; i < station_size; ++i)
    this->station_name[i] = tr("Station ") + QString::number(i);

  return 0;
}

int AdcircStationOutput::toHmdf(Hmdf *outputHmdf) {
  for (size_t i = 0; i < this->nStations; ++i) {
    HmdfStation *tempStation = new HmdfStation(outputHmdf);
    tempStation->setName(this->station_name[i]);
    tempStation->setId(this->station_name[i]);
    tempStation->setLongitude(this->longitude[i]);
    tempStation->setLatitude(this->latitude[i]);
    tempStation->setStationIndex(i);
    for (size_t j = 0; j < this->nSnaps; ++j) {
      tempStation->setNext(
          this->coldStartTime.addSecs(this->time[j]).toMSecsSinceEpoch(),
          this->data[i][j]);
    }
    outputHmdf->addStation(tempStation);
  }
  outputHmdf->setSuccess(true);
  return 0;
}
