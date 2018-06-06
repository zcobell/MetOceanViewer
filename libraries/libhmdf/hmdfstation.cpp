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
#include "hmdfstation.h"
#include <assert.h>

HmdfStation::HmdfStation(QObject *parent) : QObject(parent) {
  this->m_coordinate = QGeoCoordinate();
  this->m_name = "noname";
  this->m_id = "noid";
  this->m_isNull = true;
  this->m_stationIndex = 0;
}

void HmdfStation::clear() {
  this->m_coordinate = QGeoCoordinate();
  this->m_name = "noname";
  this->m_id = "noid";
  this->m_isNull = true;
  this->m_stationIndex = 0;
  this->m_data.clear();
  this->m_date.clear();
  return;
}

QString HmdfStation::name() const { return this->m_name; }

void HmdfStation::setName(const QString &name) { this->m_name = name; }

QString HmdfStation::id() const { return this->m_id; }

void HmdfStation::setId(const QString &id) { this->m_id = id; }

size_t HmdfStation::numSnaps() const { return this->m_data.size(); }

int HmdfStation::stationIndex() const { return this->m_stationIndex; }

void HmdfStation::setStationIndex(int stationIndex) {
  this->m_stationIndex = stationIndex;
}

qint64 HmdfStation::date(int index) const {
  assert(index >= 0 && index < this->numSnaps());
  if (index >= 0 || index < this->numSnaps())
    return this->m_date[index];
  else
    return 0;
}

double HmdfStation::data(int index) const {
  assert(index >= 0 && index < this->numSnaps());
  if (index >= 0 || index < this->numSnaps())
    return this->m_data[index];
  else
    return 0;
}

void HmdfStation::setData(const double &data, int index) {
  assert(index >= 0 && index < this->numSnaps());
  if (index >= 0 || index < this->numSnaps()) this->m_data[index] = data;
}

void HmdfStation::setDate(const qint64 &date, int index) {
  assert(index >= 0 && index < this->numSnaps());
  if (index >= 0 || index < this->numSnaps()) this->m_date[index] = date;
}

bool HmdfStation::isNull() const { return this->m_isNull; }

void HmdfStation::setIsNull(bool isNull) { this->m_isNull = isNull; }

void HmdfStation::setDate(const QVector<qint64> &date) {
  this->m_date = date;
  return;
}

void HmdfStation::setData(const QVector<double> &data) {
  this->m_data = data;
  return;
}

void HmdfStation::setNext(const qint64 &date, const double &data) {
  this->m_date.push_back(date);
  this->m_data.push_back(data);
}

QVector<qint64> HmdfStation::allDate() const { return this->m_date; }

QVector<double> HmdfStation::allData() const { return this->m_data; }

void HmdfStation::setLatitude(const double latitude) {
  this->m_coordinate.setLatitude(latitude);
}

void HmdfStation::setLongitude(const double longitude) {
  this->m_coordinate.setLongitude(longitude);
}

double HmdfStation::latitude() const { return this->m_coordinate.latitude(); }

double HmdfStation::longitude() const { return this->m_coordinate.longitude(); }

void HmdfStation::setCoordinate(const QGeoCoordinate coordinate) {
  this->m_coordinate = coordinate;
}

QGeoCoordinate *HmdfStation::coordinate() { return &this->m_coordinate; }
