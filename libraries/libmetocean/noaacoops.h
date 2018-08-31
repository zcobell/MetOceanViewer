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
#include "metocean_global.h"
#include "waterdata.h"

class NoaaCoOps : public WaterData {
  Q_OBJECT
 public:
  NoaaCoOps(Station &station, QDateTime startDate, QDateTime endDate,
            QString product, QString datum, QString units,
            QObject *parent = nullptr);

 private:
  int retrieveData(Hmdf *data);

  int parseProduct();

  int generateDateRanges(QVector<QDateTime> &startDateList,
                         QVector<QDateTime> &endDateList);

  int downloadDataFromNoaaServer(QVector<QDateTime> startDateList,
                                 QVector<QDateTime> endDateList,
                                 QVector<QString> &downloadedData);

  int readNoaaResponse(QNetworkReply *reply, QVector<QString> &retrieveData);

  int formatNoaaResponse(QVector<QString> &downloadedData, Hmdf *outputData);
  int formatNoaaResponseCsv(QVector<QString> &downloadedData, Hmdf *outputData);
  int formatNoaaResponseJson(QVector<QString> &downloadedData,
                             Hmdf *outputData);

  QString m_product;
  QStringList m_productParsed;
  QString m_datum;
  QString m_units;
  bool m_useJson;
};

#endif  // NOAACOOPS_H
