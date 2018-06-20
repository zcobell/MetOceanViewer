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
#ifndef USGSWATERDATA_H
#define USGSWATERDATA_H

#include "waterdata.h"

class UsgsWaterdata : public WaterData {
    Q_OBJECT
 public:
  UsgsWaterdata(Station &station, QDateTime startDate, QDateTime endDate,
                int databaseOption, QObject *parent = nullptr);

  int get(Hmdf *data);

 private:
  int fetch(Hmdf *data);

  QUrl buildUrl();

  int download(QUrl url, Hmdf *data);

  int readDownloadedData(QNetworkReply *reply, Hmdf *output);

  int readUsgsData(QByteArray &data, Hmdf *output);

  int m_databaseOption;
};

#endif  // USGSWATERDATA_H
