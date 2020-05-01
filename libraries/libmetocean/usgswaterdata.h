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
#ifndef USGSWATERDATA_H
#define USGSWATERDATA_H

#include "metocean_global.h"
#include "waterdata.h"

class UsgsWaterdata : public WaterData {
  Q_OBJECT
 public:
  METOCEANSHARED_EXPORT UsgsWaterdata(Station &station, QDateTime startDate, QDateTime endDate,
                int databaseOption, QObject *parent = nullptr);

  int METOCEANSHARED_EXPORT get(Hmdf *data, Datum::VDatum datum = Datum::VDatum::NullDatum);

 private:
  int fetch(Hmdf *data);

  QUrl buildUrl();

  int download(QUrl url, Hmdf *data);

  int readDownloadedData(QNetworkReply *reply, Hmdf *output);

  int readUsgsData(QByteArray &data, Hmdf *output);

  int m_databaseOption;
};

#endif  // USGSWATERDATA_H
