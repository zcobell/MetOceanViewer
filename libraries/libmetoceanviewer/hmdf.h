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
#include "metoceanviewer_global.h"
#include "timezone.h"

class Hmdf : public QObject {
  Q_OBJECT

 public:
  explicit Hmdf(QObject *parent = nullptr);

  void clear();

  enum HmdfFileType { HmdfImeds, HmdfCsv, HmdfNetCdf };

  int write(QString filename, HmdfFileType fileType);
  int write(QString filename);
  int writeImeds(QString filename);
  int writeCsv(QString filename);
  int writeNetcdf(QString filename);

  int readImeds(QString filename);
  int readNetcdf(QString filename);

  size_t nstations() const;
  // void setNstations(size_t nstations);

  QString header1() const;
  void setHeader1(const QString &header1);

  QString header2() const;
  void setHeader2(const QString &header2);

  QString header3() const;
  void setHeader3(const QString &header3);

  QString units() const;
  void setUnits(const QString &units);

  QString datum() const;
  void setDatum(const QString &datum);

  HmdfStation *station(int index);
  void setStation(int index, HmdfStation *station);
  void addStation(HmdfStation *station);

  bool success() const;
  void setSuccess(bool success);

  bool null() const;
  void setNull(bool null);

  void dataBounds(qint64 &dateMin, qint64 &dateMax, double &minValue,
                  double &maxValue);

 private:
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
