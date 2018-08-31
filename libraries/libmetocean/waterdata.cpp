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
#include "waterdata.h"

WaterData::WaterData(Station &station, QDateTime startDate, QDateTime endDate,
                     QObject *parent)
    : QObject(parent) {
  this->m_errorString = QString();
  this->m_station = station;
  this->m_startDate = startDate;
  this->m_endDate = endDate;
  this->m_timezone = new Timezone(this);
}

int WaterData::get(Hmdf *data) { return this->retrieveData(data); }

QString WaterData::errorString() const { return this->m_errorString; }

int WaterData::retrieveData(Hmdf *data) {
  Q_UNUSED(data);
  return 0;
}

QDateTime WaterData::endDate() const { return this->m_endDate; }

void WaterData::setEndDate(const QDateTime &endDate) {
  this->m_endDate = endDate;
}

Timezone *WaterData::getTimezone() const { return this->m_timezone; }

void WaterData::setTimezone(Timezone *timezone) { this->m_timezone = timezone; }

QDateTime WaterData::startDate() const { return this->m_startDate; }

void WaterData::setStartDate(const QDateTime &startDate) {
  this->m_startDate = startDate;
}

Station WaterData::station() const { return this->m_station; }

void WaterData::setStation(const Station &station) {
  this->m_station = station;
}

void WaterData::setErrorString(const QString &errorString) {
  this->m_errorString = errorString;
}
