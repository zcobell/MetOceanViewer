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
#ifndef NOAACOOPS_H
#define NOAACOOPS_H

#include <QNetworkReply>
#include <QObject>
#include "hmdf.h"

class NoaaCoOps : public QObject {
  Q_OBJECT
 public:
  NoaaCoOps(QString stationId, QString stationName, QGeoCoordinate location,
            QString product, QString datum, QString units, QDateTime startDate,
            QDateTime endDate, QObject *parent = nullptr);

  int get(Hmdf *data);

  QString errorString();

 private:
  int downloadData(Hmdf *data);

  int generateDateRanges(QVector<QDateTime> &startDateList,
                         QVector<QDateTime> &endDateList);

  int downloadDataFromNoaaServer(QVector<QDateTime> startDateList,
                                 QVector<QDateTime> endDateList,
                                 QVector<QByteArray> &downloadedData);

  int readNoaaResponse(QNetworkReply *reply, QVector<QByteArray> &downloadData);

  int formatNoaaResponse(QVector<QByteArray> &downloadedData, Hmdf *outputData);

  QString m_stationId;
  QString m_stationName;
  QString m_product;
  QString m_datum;
  QString m_units;
  QString m_errorString;
  QGeoCoordinate m_location;
  QDateTime m_startDate;
  QDateTime m_endDate;
};

#endif  // NOAACOOPS_H
