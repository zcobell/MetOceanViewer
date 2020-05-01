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
#ifndef HMDF_H
#define HMDF_H

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <ctime>
#include <string>
#include <vector>

#include "hmdfstation.h"
#include "metocean_global.h"
#include "timezone.h"

class Hmdf : public QObject {
  Q_OBJECT

 public:
  explicit METOCEANSHARED_EXPORT Hmdf(QObject *parent = nullptr);

  METOCEANSHARED_EXPORT ~Hmdf();

  void METOCEANSHARED_EXPORT clear();

  enum HmdfFileType { HmdfImeds, HmdfCsv, HmdfNetCdf };

  int METOCEANSHARED_EXPORT write(QString filename, HmdfFileType fileType);
  int METOCEANSHARED_EXPORT write(QString filename);
  int METOCEANSHARED_EXPORT writeImeds(QString filename);
  int METOCEANSHARED_EXPORT writeCsv(QString filename);
  int METOCEANSHARED_EXPORT writeNetcdf(QString filename);

  int METOCEANSHARED_EXPORT readImeds(QString filename);
  int METOCEANSHARED_EXPORT readNetcdf(QString filename);

  size_t METOCEANSHARED_EXPORT nstations() const;
  // void setNstations(size_t nstations);

  QString METOCEANSHARED_EXPORT header1() const;
  void METOCEANSHARED_EXPORT setHeader1(const QString &header1);

  QString METOCEANSHARED_EXPORT header2() const;
  void METOCEANSHARED_EXPORT setHeader2(const QString &header2);

  QString METOCEANSHARED_EXPORT header3() const;
  void METOCEANSHARED_EXPORT setHeader3(const QString &header3);

  QString METOCEANSHARED_EXPORT units() const;
  void METOCEANSHARED_EXPORT setUnits(const QString &units);

  QString METOCEANSHARED_EXPORT datum() const;
  void METOCEANSHARED_EXPORT setDatum(const QString &datum);

  HmdfStation METOCEANSHARED_EXPORT *station(int index);
  void METOCEANSHARED_EXPORT setStation(int index, HmdfStation *station);
  void METOCEANSHARED_EXPORT addStation(HmdfStation *station);

  bool METOCEANSHARED_EXPORT success() const;
  void METOCEANSHARED_EXPORT setSuccess(bool success);

  bool METOCEANSHARED_EXPORT null() const;
  void METOCEANSHARED_EXPORT setNull(bool null);

  void METOCEANSHARED_EXPORT dataBounds(qint64 &dateMin, qint64 &dateMax,
                                        double &minValue, double &maxValue);

  bool METOCEANSHARED_EXPORT applyDatumCorrection(const Station &s,
                                                  const Datum::VDatum &datum);
  bool METOCEANSHARED_EXPORT applyDatumCorrection(QVector<Station> &s,
                                                  Datum::VDatum datum);

 private:
  void init();
  void deallocNcArrays(long long *time, double *data, char *name, char *id);

  //...Variables
  bool m_success, m_null;

  Timezone m_tz;
  QString m_header1;
  QString m_header2;
  QString m_header3;
  QString m_units;
  QString m_datum;
  QVector<HmdfStation *> m_station;
};

#endif  // HMDF_H
