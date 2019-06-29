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
#ifndef DRIVER_H
#define DRIVER_H

#include <QDateTime>
#include <QObject>
#include "hmdf.h"
#include "station.h"
#include "stationlocations.h"

class MetOceanData : public QObject {
  Q_OBJECT
 public:
  enum serviceTypes { NOAA, USGS, NDBC, XTIDE, UNKNOWNSERVICE };

  explicit MetOceanData(QObject *parent = nullptr);
  explicit MetOceanData(serviceTypes service, QStringList station, int product,
                        int datum, QDateTime startDate, QDateTime endDate,
                        QString outputFile, QObject *parent = nullptr);
  explicit MetOceanData(QString crmsFile, QString outputFile,
                        QObject *parent = nullptr);

  static QStringList selectStations(serviceTypes service, double x1, double y1,
                                    double x2, double y2);

  static QString selectNearestStation(serviceTypes service, double x, double y);

  int service() const;
  void setService(int service);

  QStringList station() const;
  void setStation(QStringList station);

  QDateTime startDate() const;
  void setStartDate(const QDateTime &startDate);

  QDateTime endDate() const;
  void setEndDate(const QDateTime &endDate);

  QString outputFile() const;
  void setOutputFile(const QString &outputFile);

  void setLoggingActive();
  void setLoggingInactive();

  int getDatum() const;
  void setDatum(int datum);

  static StationLocations::MarkerType serviceToMarkerType(
      MetOceanData::serviceTypes type);
  static bool findStation(QStringList name, StationLocations::MarkerType type,
                          QVector<Station> &s);

 signals:
  void finished();
  void status(QString, int);
  void error(QString);
  void warning(QString);

 public slots:
  virtual void run();
  void showError(QString);
  void showStatus(QString, int);
  void showWarning(QString);

 private:
  void getNoaaData();
  void getUsgsData();
  void getNdbcData();
  void getXtideData();
  void processCrmsData();

  QString noaaIndexToProduct();
  QString noaaIndexToDatum();
  QString noaaIndexToUnits();

  int printAvailableProducts(Hmdf *data);

  bool m_doCrms;
  int m_service;
  QStringList m_station;
  int m_product;
  int m_datum;
  QDateTime m_startDate;
  QDateTime m_endDate;
  QString m_outputFile;
  QString m_crmsFile;
};

#endif  // DRIVER_H
