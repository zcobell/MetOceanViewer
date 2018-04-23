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
#ifndef ADCIRCSTATIONOUTPUT_H
#define ADCIRCSTATIONOUTPUT_H

#include <QDateTime>
#include <QObject>
#include <QVector>

class Imeds;

class AdcircStationOutput : public QObject {
  Q_OBJECT
public:
  explicit AdcircStationOutput(QObject *parent = nullptr);

  int read(QString AdcircFile, QDateTime coldStart);
  int read(QString AdcircFile, QString AdcircStationFile, QDateTime coldStart);
  QString errorString();
  int error();
  int toIMEDS(Imeds *outputImeds);

private:
  int readAscii(QString AdcircOutputFile, QString AdcircStationFile);

  int readNetCDF(QString AdicrcOutputFile);

  int nStations;
  int nSnaps;
  int _error;
  int _ncerr;

  QDateTime coldStartTime;

  QVector<double> latitude;
  QVector<double> longitude;
  QVector<double> time;

  QVector<QVector<double>> data;

  QVector<QString> station_name;
};

#endif // ADCIRCSTATIONOUTPUT_H
