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
#include "waterdata.h"

WaterData::WaterData(const MovStation &station, const QDateTime startDate,
                     const QDateTime endDate)
    : m_errorString(std::string()),
      m_station(station),
      m_startDate(startDate),
      m_endDate(endDate) {}

int WaterData::get(Hmdf::HmdfData *data, Datum::VDatum datum) {
  return this->retrieveData(data, datum);
}

std::string WaterData::errorString() const { return this->m_errorString; }

int WaterData::retrieveData(Hmdf::HmdfData *data, Datum::VDatum datum) {
  Q_UNUSED(data);
  Q_UNUSED(datum);
  return 0;
}

QDateTime WaterData::endDate() const { return this->m_endDate; }

void WaterData::setEndDate(const QDateTime &endDate) {
  this->m_endDate = endDate;
}

QDateTime WaterData::startDate() const { return this->m_startDate; }

void WaterData::setStartDate(const QDateTime &startDate) {
  this->m_startDate = startDate;
}

MovStation WaterData::station() const { return this->m_station; }

void WaterData::setStation(const MovStation &station) {
  this->m_station = station;
}

void WaterData::setErrorString(const std::string &errorString) {
  this->m_errorString = errorString;
}
