/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2015  Zach Cobell
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
#include "movAdcircStationOutput.h"
#include "movErrors.h"
#include "movImeds.h"
#include <QFile>
#include <QtMath>
#include <netcdf.h>

MovAdcircStationOutput::MovAdcircStationOutput(QObject *parent)
    : QObject(parent) {
  this->_error = ERR_NOERR;
  this->_ncerr = NC_NOERR;
  this->nStations = 0;
  this->nSnaps = 0;
}

int MovAdcircStationOutput::error() { return this->_error; }

QString MovAdcircStationOutput::errorString() { return "errorString"; }

int MovAdcircStationOutput::read(QString AdcircFile, QString AdcircStationFile,
                                 QDateTime coldStart) {
  this->coldStartTime = coldStart;
  this->_error = this->readAscii(AdcircFile, AdcircStationFile);
  return this->_error;
}

int MovAdcircStationOutput::read(QString AdcircFile, QDateTime coldStart) {
  this->coldStartTime = coldStart;
  this->_error = this->readNetCDF(AdcircFile);
  return this->_error;
}

int MovAdcircStationOutput::readAscii(QString AdcircOutputFile,
                                      QString AdcircStationFile) {
  QFile MyFile(AdcircOutputFile), StationFile(AdcircStationFile);
  QString header1, header2, TempLine;
  QStringList headerData, TempList;
  int nColumns;

  // Check if we can open the file
  if (!MyFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    this->_error = ERR_CANNOT_OPEN_FILE;
    return this->_error;
  }

  if (!StationFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    this->_error = ERR_CANNOT_OPEN_FILE;
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

  for (int i = 0; i < this->nStations; ++i)
    this->data[i].resize(this->nSnaps);

  for (int i = 0; i < this->nSnaps; ++i) {
    TempLine = MyFile.readLine().simplified();
    TempList = TempLine.split(" ");
    this->time[i] = TempList.value(1).toDouble();
    for (int j = 0; j < this->nStations; ++j) {
      TempLine = MyFile.readLine().simplified();
      TempList = TempLine.split(" ");
      this->data[j][i] = TempList.value(1).toDouble();
      if (nColumns == 2)
        this->data[j][i] = qPow(qPow(this->data[j][i], 2) +
                                    qPow(TempList.value(2).toDouble(), 2),
                                2);
    }
  }
  MyFile.close();

  // Now read the station location file
  TempLine = StationFile.readLine().simplified();
  TempList = TempLine.split(" ");
  int TempStations = TempList.value(0).toInt();
  if (TempStations != this->nStations)
    return ERR_WRONG_NUMBER_OF_STATIONS;

  this->longitude.resize(this->nStations);
  this->latitude.resize(this->nStations);
  this->station_name.resize(this->nStations);

  for (int i = 0; i < TempStations; ++i) {
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

  return ERR_NOERR;
}

int MovAdcircStationOutput::readNetCDF(QString AdcircOutputFile) {

  size_t station_size, time_size, startIndex;
  int i, j, ierr, time_size_int, station_size_int;
  int ncid, varid_zeta, varid_zeta2, varid_lat, varid_lon, varid_time;
  int dimid_time, dimid_station;
  bool isVector;
  double Temp;

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
  this->_error = nc_open(AdcircOutputFile.toUtf8(), NC_NOWRITE, &ncid);
  if (this->_error != NC_NOERR) {
    this->_ncerr = this->_error;
    this->_error = ERR_NETCDF;
    return this->_error;
  }

  // Get the dimension ids
  this->_error = nc_inq_dimid(ncid, "time", &dimid_time);
  if (this->_error != NC_NOERR) {
    this->_ncerr = this->_error;
    this->_error = ERR_NETCDF;
    return this->_error;
  }

  this->_error = nc_inq_dimid(ncid, "station", &dimid_station);
  if (this->_error != NC_NOERR) {
    this->_ncerr = this->_error;
    this->_error = ERR_NETCDF;
    return this->_error;
  }

  // Find out the dimension size
  this->_error = nc_inq_dimlen(ncid, dimid_time, &time_size);
  if (this->_error != NC_NOERR) {
    this->_ncerr = this->_error;
    this->_error = ERR_NETCDF;
    return this->_error;
  }

  this->_error = nc_inq_dimlen(ncid, dimid_station, &station_size);
  if (this->_error != NC_NOERR) {
    this->_ncerr = this->_error;
    this->_error = ERR_NETCDF;
    return this->_error;
  }

  station_size_int = static_cast<unsigned int>(station_size);
  time_size_int = static_cast<unsigned int>(time_size);

  // Find the variable in the NetCDF file
  for (i = 0; i < 6; i++) {
    ierr = nc_inq_varid(ncid, netcdf_types[i].toUtf8(), &varid_zeta);

    // If we found the variable, we're done
    if (ierr == NC_NOERR) {
      if (i == 1 || i == 4) {
        isVector = true;
        this->_error =
            nc_inq_varid(ncid, netcdf_types[i + 1].toUtf8(), &varid_zeta2);
        if (this->_error != NC_NOERR) {
          this->_ncerr = this->_error;
          this->_error = ERR_NETCDF;
          return this->_error;
        }
      } else
        isVector = false;

      break;
    }

    // If we're at the end of the array
    // and haven't quit yet, that's a problem
    if (i == 5)
      return ERR_NO_VARIABLE_FOUND;
  }

  // Size the output variables
  this->latitude.resize(station_size_int);
  this->longitude.resize(station_size_int);
  this->nStations = station_size_int;
  this->nSnaps = time_size_int;
  this->time.resize(time_size_int);
  this->data.resize(station_size_int);
  for (i = 0; i < station_size_int; ++i)
    this->data[i].resize(time_size_int);

  // Read the station locations and times
  this->_error = nc_inq_varid(ncid, "time", &varid_time);
  if (this->_error != NC_NOERR) {
    this->_ncerr = this->_error;
    this->_error = ERR_NETCDF;
    return this->_error;
  }

  this->_error = nc_inq_varid(ncid, "x", &varid_lon);
  if (this->_error != NC_NOERR) {
    this->_ncerr = this->_error;
    this->_error = ERR_NETCDF;
    return this->_error;
  }

  this->_error = nc_inq_varid(ncid, "y", &varid_lat);
  if (this->_error != NC_NOERR) {
    this->_ncerr = this->_error;
    this->_error = ERR_NETCDF;
    return this->_error;
  }

  for (j = 0; j < time_size_int; j++) {
    startIndex = static_cast<size_t>(j);
    this->_error = nc_get_var1(ncid, varid_time, &startIndex, &Temp);
    if (this->_error != NC_NOERR) {
      this->_ncerr = this->_error;
      this->_error = ERR_NETCDF;
      return this->_error;
    }

    this->time[j] = Temp;
  }

  for (j = 0; j < station_size_int; j++) {
    startIndex = static_cast<size_t>(j);
    this->_error = nc_get_var1(ncid, varid_lon, &startIndex, &Temp);
    if (this->_error != NC_NOERR) {
      this->_ncerr = this->_error;
      this->_error = ERR_NETCDF;
      return this->_error;
    }

    this->longitude[j] = Temp;

    this->_error = nc_get_var1(ncid, varid_lat, &startIndex, &Temp);
    if (this->_error != NC_NOERR) {
      this->_ncerr = this->_error;
      this->_error = ERR_NETCDF;
      return this->_error;
    }

    this->latitude[j] = Temp;
  }

  double *tempVar1 = (double *)malloc(sizeof(double) * time_size_int);
  double *tempVar2 = (double *)malloc(sizeof(double) * time_size_int);

  // Loop over the stations, reading the data into memory
  for (i = 0; i < station_size_int; ++i) {

    // Read from netCDF
    start[0] = static_cast<size_t>(0);
    start[1] = static_cast<size_t>(i);
    count[0] = static_cast<size_t>(time_size_int);
    count[1] = static_cast<size_t>(1);
    this->_error = nc_get_vara(ncid, varid_zeta, start, count, tempVar1);
    if (this->_error != NC_NOERR) {
      free(tempVar1);
      free(tempVar2);
      this->_ncerr = this->_error;
      this->_error = ERR_NETCDF;
      return this->_error;
    }

    if (isVector) {
      this->_error = nc_get_vara(ncid, varid_zeta2, start, count, tempVar2);
      if (this->_error != NC_NOERR) {
        free(tempVar1);
        free(tempVar2);
        this->_ncerr = this->_error;
        this->_error = ERR_NETCDF;
        return this->_error;
      }
      for (j = 0; j < time_size_int; j++)
        this->data[i][j] =
            qSqrt(qPow(tempVar1[j], 2.0) + qPow(tempVar2[j], 2.0));
    } else {
      // Place in the output variable
      for (j = 0; j < time_size_int; ++j)
        this->data[i][j] = tempVar1[j];
    }
  }
  this->_error = nc_close(ncid);
  if (this->_error != NC_NOERR) {
    free(tempVar1);
    free(tempVar2);
    this->_ncerr = this->_error;
    this->_error = ERR_NETCDF;
    return this->_error;
  }

  // Finally, name the stations the default names for now. Later
  // we can get fancy and try to get the ADCIRC written names in
  // the NetCDF file
  this->station_name.resize(station_size_int);
  for (i = 0; i < station_size_int; ++i)
    this->station_name[i] = tr("Station ") + QString::number(i);

  free(tempVar1);
  free(tempVar2);

  return 0;
}

MovImeds *MovAdcircStationOutput::toIMEDS() {
  MovImeds *outputIMEDS = new MovImeds(this);

  outputIMEDS->nstations = this->nStations;
  outputIMEDS->station.resize(outputIMEDS->nstations);

  for (int i = 0; i < outputIMEDS->nstations; ++i) {
    outputIMEDS->station[i] = new MovImedsStation(this);
    outputIMEDS->station[i]->data.resize(this->nSnaps);
    outputIMEDS->station[i]->date.resize(this->nSnaps);
    outputIMEDS->station[i]->StationName = this->station_name[i];
    outputIMEDS->station[i]->NumSnaps = this->nSnaps;
    outputIMEDS->station[i]->longitude = this->longitude[i];
    outputIMEDS->station[i]->latitude = this->latitude[i];
    outputIMEDS->station[i]->StationIndex = i;
    for (int j = 0; j < this->nSnaps; ++j) {
      outputIMEDS->station[i]->date[j] =
          this->coldStartTime.addSecs(this->time[j]);
      outputIMEDS->station[i]->data[j] = this->data[i][j];
    }
  }
  outputIMEDS->success = true;
  return outputIMEDS;
}
