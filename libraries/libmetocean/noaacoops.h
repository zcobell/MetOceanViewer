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
#ifndef NOAACOOPS_H
#define NOAACOOPS_H

#include <QNetworkReply>
#include <QObject>

#include "metocean_global.h"
#include "waterdata.h"

class NoaaCoOps : public WaterData {
 public:
  METOCEANSHARED_EXPORT NoaaCoOps(
      const MovStation &station, const QDateTime startDate,
      const QDateTime endDate, const std::string &product,
      const std::string &datum, const bool useVdatum, const std::string &units);

 private:
  int retrieveData(Hmdf::HmdfData *data,
                   Datum::VDatum datum = Datum::VDatum::NullDatum);

  int parseProduct();

  int generateDateRanges(std::vector<QDateTime> &startDateList,
                         std::vector<QDateTime> &endDateList);

  int downloadDataFromNoaaServer(std::vector<QDateTime> startDateList,
                                 std::vector<QDateTime> endDateList,
                                 std::vector<std::string> &downloadedData);

  int readNoaaResponse(QNetworkReply *reply,
                       std::vector<std::string> &retrieveData);

  int formatNoaaResponse(std::vector<std::string> &downloadedData,
                         Hmdf::HmdfData *outputData);
  int formatNoaaResponseCsv(std::vector<std::string> &downloadedData,
                            Hmdf::HmdfData *outputData);
  int formatNoaaResponseJson(std::vector<std::string> &downloadedData,
                             Hmdf::HmdfData *outputData);
  void parseCsvToValuePair(std::string &data, QDateTime &date, double &value);

  std::string m_product;
  std::vector<std::string> m_productParsed;
  std::string m_datum;
  std::string m_units;
  bool m_useJson, m_useVdatum;
};

#endif  // NOAACOOPS_H
