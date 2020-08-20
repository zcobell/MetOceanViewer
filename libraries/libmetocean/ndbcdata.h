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

#include <unordered_map>

#include "metocean_global.h"
#include "waterdata.h"

class NdbcData : public WaterData {
 public:
  METOCEANSHARED_EXPORT NdbcData(MovStation &station, QDateTime startDate,
                                 QDateTime endDate);

  static std::array<std::string_view, 15> METOCEANSHARED_EXPORT dataTypes();

  static std::array<std::string_view, 15> METOCEANSHARED_EXPORT dataNames();

  static std::unordered_map<std::string_view, std::string_view>
      METOCEANSHARED_EXPORT dataMap();

  static std::string_view METOCEANSHARED_EXPORT
  units(const std::string_view &parameter);

 private:
  int retrieveData(Hmdf::HmdfData *data,
                   Datum::VDatum datum = Datum::VDatum::NullDatum);

  static std::unordered_map<std::string_view, std::string_view>
  buildDataNameMap();

  int download(QUrl url, std::vector<std::vector<std::string>> &dldata);

  int readNdbcResponse(QNetworkReply *reply,
                       std::vector<std::vector<std::string>> &data);

  int formatNdbcResponse(std::vector<std::vector<std::string>> &serverResponse,
                         Hmdf::HmdfData *data);

  std::unordered_map<std::string_view, std::string_view> m_dataNameMap;
};

#endif  // NDBCDATA_H
