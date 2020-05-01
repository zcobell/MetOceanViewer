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
#ifndef NETCDFTIMESERIES_H
#define NETCDFTIMESERIES_H

#include <QDateTime>
#include <QObject>
#include <QVector>
#include "hmdf.h"
#include "metocean_global.h"

class NetcdfTimeseries : public QObject {
  Q_OBJECT
 public:
  explicit METOCEANSHARED_EXPORT NetcdfTimeseries(QObject *parent = nullptr);

  int METOCEANSHARED_EXPORT read();

  int METOCEANSHARED_EXPORT toHmdf(Hmdf *hmdf);

  QString METOCEANSHARED_EXPORT filename() const;
  void METOCEANSHARED_EXPORT setFilename(const QString &filename);

  int METOCEANSHARED_EXPORT epsg() const;
  void METOCEANSHARED_EXPORT setEpsg(int epsg);

  static int METOCEANSHARED_EXPORT getEpsg(QString file);

 private:
  QString m_filename;
  QString m_units;
  QString m_verticalDatum;
  QString m_horizontalProjection;
  int m_epsg;
  size_t m_numStations;

  QVector<double> m_fillValue;
  QVector<double> m_xcoor;
  QVector<double> m_ycoor;
  QVector<size_t> m_stationLength;
  QVector<QString> m_stationName;
  QVector<QVector<qint64> > m_time;
  QVector<QVector<double> > m_data;
};

#endif  // NETCDFTIMESERIES_H
