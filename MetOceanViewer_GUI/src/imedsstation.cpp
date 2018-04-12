/**-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2015  Zach Cobell
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
#include "imedsstation.h"
#include <assert.h>

ImedsStation::ImedsStation() {
  this->m_coordinate.setLatitude(0.0);
  this->m_coordinate.setLongitude(0.0);
  this->m_data.resize(1);
  this->m_date.resize(1);
  this->m_numSnaps = 0;
  this->m_id = QString();
  this->m_name = QString();
  this->m_stationIndex = 0;
  this->m_isNull = false;
}

QGeoCoordinate *ImedsStation::coordinate() { return &this->m_coordinate; }

void ImedsStation::setCoordinate(const QGeoCoordinate &coordinate) {
  this->m_coordinate = coordinate;
}

QString ImedsStation::name() const { return this->m_name; }

void ImedsStation::setName(const QString &name) { this->m_name = name; }

QString ImedsStation::id() const { return this->m_id; }

void ImedsStation::setId(const QString &id) { this->m_id = id; }

int ImedsStation::numSnaps() const { return this->m_numSnaps; }

void ImedsStation::setNumSnaps(int numSnaps) {
  this->m_numSnaps = numSnaps;
  this->m_data.resize(this->m_numSnaps);
  this->m_date.resize(this->m_numSnaps);
}

int ImedsStation::stationIndex() const { return this->m_stationIndex; }

void ImedsStation::setStationIndex(int stationIndex) {
  this->m_stationIndex = stationIndex;
}

qint64 ImedsStation::date(int index) const {
  assert(index >= 0 && index < this->m_numSnaps);
  if (index >= 0 || index < this->m_numSnaps)
    return this->m_date[index];
  else
    return 0;
}

double ImedsStation::data(int index) const {
  assert(index >= 0 && index < this->m_numSnaps);
  if (index >= 0 || index < this->m_numSnaps)
    return this->m_data[index];
  else
    return 0;
}

void ImedsStation::setData(const double &data, int index) {
  assert(index >= 0 && index < this->m_numSnaps);
  if (index >= 0 || index < this->m_numSnaps) this->m_data[index] = data;
}

void ImedsStation::setDate(const qint64 &date, int index) {
  assert(index >= 0 && index < this->m_numSnaps);
  if (index >= 0 || index < this->m_numSnaps) this->m_date[index] = date;
}

bool ImedsStation::isNull() const { return this->m_isNull; }

void ImedsStation::setIsNull(bool isNull) { this->m_isNull = isNull; }

void ImedsStation::setDate(const QVector<qint64> &date) {
  assert(this->m_numSnaps == date.length());
  this->m_date = date;
  return;
}

void ImedsStation::setData(const QVector<double> &data) {
  assert(this->m_numSnaps == data.length());
  this->m_data = data;
  return;
}

void ImedsStation::setNext(const qint64 &date, const double &data) {
  this->m_date.push_back(date);
  this->m_data.push_back(data);
  this->m_numSnaps = this->m_data.size();
}

QVector<qint64> ImedsStation::allDate() const { return this->m_date; }

QVector<double> ImedsStation::allData() const { return this->m_data; }

void ImedsStation::setLatitude(const double latitude) {
  this->m_coordinate.setLatitude(latitude);
}

void ImedsStation::setLongitude(const double longitude) {
  this->m_coordinate.setLongitude(longitude);
}
