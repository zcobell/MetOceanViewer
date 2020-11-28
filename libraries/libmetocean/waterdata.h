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
#ifndef WATERDATA_H
#define WATERDATA_H

#include <QNetworkReply>
#include <string>

#include "date.h"
#include "datum.h"
#include "hmdf.h"
#include "metocean_global.h"
#include "movStation.h"

class WaterData {
 public:
  explicit METOCEANSHARED_EXPORT WaterData(const MovStation &station,
                                           const QDateTime startDate,
                                           const QDateTime endDate);

  int METOCEANSHARED_EXPORT get(Hmdf::HmdfData *data,
                                Datum::VDatum datum = Datum::VDatum::NullDatum);

  [[nodiscard]] std::string METOCEANSHARED_EXPORT errorString() const;

 protected:
  virtual int retrieveData(Hmdf::HmdfData *data, Datum::VDatum datum);

  void setErrorString(const std::string &errorString);

  [[nodiscard]] MovStation station() const;
  void setStation(const MovStation &station);

  [[nodiscard]] QDateTime startDate() const;
  void setStartDate(const QDateTime &startDate);

  [[nodiscard]] QDateTime endDate() const;
  void setEndDate(const QDateTime &endDate);

 private:
  std::string m_errorString;
  MovStation m_station;
  QDateTime m_startDate;
  QDateTime m_endDate;
};

#endif  // WATERDATA_H
