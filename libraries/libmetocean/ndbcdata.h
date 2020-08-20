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
#ifndef NDBCDATA_H
#define NDBCDATA_H

#include <QMap>

#include "metocean_global.h"
#include "waterdata.h"

class NdbcData : public WaterData {
 public:
  METOCEANSHARED_EXPORT NdbcData(MovStation &station, QDateTime startDate,
                                 QDateTime endDate);

  static QStringList METOCEANSHARED_EXPORT dataTypes();
  static QStringList METOCEANSHARED_EXPORT dataNames();
  static QMap<QString, QString> METOCEANSHARED_EXPORT dataMap();
  static QString METOCEANSHARED_EXPORT units(const QString &parameter);

 private:
  int retrieveData(Hmdf::HmdfData *data, Datum::VDatum datum = Datum::VDatum::NullDatum);
  static QMap<QString, QString> buildDataNameMap();
  int download(QUrl url, QVector<QStringList> &dldata);
  int readNdbcResponse(QNetworkReply *reply, QVector<QStringList> &data);
  int formatNdbcResponse(QVector<QStringList> &serverResponse, Hmdf::HmdfData *data);

  QMap<QString, QString> m_dataNameMap;
};

#endif  // NDBCDATA_H
