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
#ifndef CRMSDATA_H
#define CRMSDATA_H

#include <unordered_map>

#include "waterdata.h"

class CrmsData : public WaterData {
 public:
  METOCEANSHARED_EXPORT CrmsData(MovStation &station, QDateTime startDate,
                                 QDateTime endDate,
                                 const std::vector<std::string> &header,
                                 const std::unordered_map<std::string, size_t> &mapping,
                                 const std::string &filename);

  static bool METOCEANSHARED_EXPORT
  readHeader(const std::string &filename, std::vector<std::string> &header);

  static bool METOCEANSHARED_EXPORT generateStationMapping(
      const std::string &filename, std::unordered_map<std::string, size_t> &mapping);

  static bool METOCEANSHARED_EXPORT readStationList(
      const std::string &filename, std::vector<double> &latitude,
      std::vector<double> &longitude, std::vector<std::string> &stationNames,
      std::vector<QDateTime> &startDate, std::vector<QDateTime> &endDate);

  static bool METOCEANSHARED_EXPORT inquireCrmsStatus(const std::string &filename);

 private:
  int retrieveData(Hmdf::HmdfData *data, Datum::VDatum datum);

  QDateTime m_startTime;
  QDateTime m_endTime;
  std::string m_filename;
  std::vector<std::string> m_header;
  std::unordered_map<std::string, size_t> m_mapping;
};

#endif  // CRMSDATA_H
