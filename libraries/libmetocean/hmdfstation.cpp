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
#include "hmdfstation.h"

HmdfStation::HmdfStation(QObject *parent) : QObject(parent) {
  this->m_coordinate = QGeoCoordinate();
  this->m_name = "noname";
  this->m_id = "noid";
  this->m_isNull = true;
  this->m_stationIndex = 0;
  this->m_nullValue = HmdfStation::nullDataValue();
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
  Q_ASSERT(index >= 0 && index < this->numSnaps());
  if (index >= 0 || index < this->numSnaps())
    return this->m_date[index];
  else
    return 0;
}

double HmdfStation::data(int index) const {
  Q_ASSERT(index >= 0 && index < this->numSnaps());
  if (index >= 0 || index < this->numSnaps())
    return this->m_data[index];
  else
    return 0;
}

void HmdfStation::setData(const double &data, int index) {
  Q_ASSERT(index >= 0 && index < this->numSnaps());
  if (index >= 0 || index < this->numSnaps()) this->m_data[index] = data;
}

void HmdfStation::setDate(const qint64 &date, int index) {
  Q_ASSERT(index >= 0 && index < this->numSnaps());
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

void HmdfStation::setData(const QVector<float> &data) {
  this->m_data.resize(data.size());
  for (size_t i = 0; i < data.size(); ++i) {
    this->m_data[i] = static_cast<double>(data[i]);
  }
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

void HmdfStation::dataBounds(qint64 &minDate, qint64 &maxDate, double &minValue,
                             double &maxValue) {
  minDate = *std::min_element(this->m_date.begin(), this->m_date.end());
  maxDate = *std::max_element(this->m_date.begin(), this->m_date.end());

  std::vector<double> sortedData(this->m_data.begin(),this->m_data.end());
  std::sort(sortedData.begin(), sortedData.end());

  if (sortedData.front() != sortedData.back()) {
    minValue = *std::upper_bound(sortedData.begin(), sortedData.end(),
                                 this->nullDataValue());
  } else {
    minValue = sortedData.front();
  }
  maxValue = sortedData.back();

  return;
}

double HmdfStation::nullValue() const { return this->m_nullValue; }

void HmdfStation::setNullValue(double nullValue) {
  this->m_nullValue = nullValue;
}

int HmdfStation::applyDatumCorrection(Station s, Datum::VDatum datum) {
  if (datum == Datum::VDatum::NullDatum) return 0;

  double shift = 0.0;
  if (datum == Datum::VDatum::MLLW)
    shift = s.mllwOffset();
  else if (datum == Datum::VDatum::MLW)
    shift = s.mlwOffset();
  else if (datum == Datum::VDatum::MSL)
    shift = s.mslOffset();
  else if (datum == Datum::VDatum::MHW)
    shift = s.mhwOffset();
  else if (datum == Datum::VDatum::MHHW)
    shift = s.mhhwOffset();
  else if (datum == Datum::VDatum::NGVD29)
    shift = s.ngvd29Offset();
  else if (datum == Datum::VDatum::NAVD88)
    shift = s.navd88Offset();

  if (s.isNullOffset(shift)) return 1;

  for (auto &d : this->m_data) {
    d += shift;
  }

  return 0;
}
