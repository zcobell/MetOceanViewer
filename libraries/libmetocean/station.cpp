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
#include "station.h"

Station::Station()
    : m_coordinate(QGeoCoordinate()),
      m_id(QString()),
      m_name(QString()),
      m_measured(0.0),
      m_modeled(0.0),
      m_selected(false),
      m_category(0),
      m_startValidDate(QDateTime(QDate(1900, 1, 1), QTime(0, 0, 0))),
      m_endValidDate(QDateTime(QDate(2050, 1, 1), QTime(0, 0, 0))),
      m_active(true),
      m_mslOffset(0.0),
      m_navd88Offset(0.0),
      m_mllwOffset(0.0),
      m_mlwOffset(0.0),
      m_mhwOffset(0.0),
      m_mhhwOffset(0.0),
      m_ngvd29Offset(0.0) {}

Station::Station(QGeoCoordinate coordinate, QString id, QString name,
                 double measured, double modeled, int category, bool active,
                 QDateTime startValidDate, QDateTime endValidDate)
    : m_coordinate(coordinate),
      m_id(id),
      m_name(name),
      m_measured(measured),
      m_modeled(modeled),
      m_selected(false),
      m_category(category),
      m_startValidDate(startValidDate),
      m_endValidDate(endValidDate),
      m_active(active),
      m_mslOffset(0.0),
      m_navd88Offset(0.0),
      m_mllwOffset(0.0),
      m_mlwOffset(0.0),
      m_mhwOffset(0.0),
      m_mhhwOffset(0.0),
      m_ngvd29Offset(0.0) {}

Station::~Station() {}

QGeoCoordinate Station::coordinate() const { return this->m_coordinate; }

void Station::setLatitude(const double latitude) {
  this->m_coordinate.setLatitude(latitude);
}

void Station::setLongitude(const double longitude) {
  this->m_coordinate.setLongitude(longitude);
}

void Station::setCoordinate(const QGeoCoordinate &coordinate) {
  this->m_coordinate = coordinate;
}

QString Station::name() const { return this->m_name; }

void Station::setName(const QString &name) { this->m_name = name; }

QString Station::id() const { return this->m_id; }

void Station::setId(const QString &id) { this->m_id = id; }

bool Station::selected() const { return this->m_selected; }

void Station::setSelected(bool selected) { this->m_selected = selected; }

double Station::modeled() const { return m_modeled; }

void Station::setModeled(double modeled) { m_modeled = modeled; }

double Station::measured() const { return m_measured; }

void Station::setMeasured(double measured) { m_measured = measured; }

int Station::category() const { return this->m_category; }

void Station::setCategory(int category) { this->m_category = category; }

double Station::difference() const {
  return this->m_measured - this->m_modeled;
}

QDateTime Station::startValidDate() const { return this->m_startValidDate; }

void Station::setStartValidDate(const QDateTime &startValidDate) {
  this->m_startValidDate = startValidDate;
}

QDateTime Station::endValidDate() const { return this->m_endValidDate; }

void Station::setEndValidDate(const QDateTime &endValidDate) {
  this->m_endValidDate = endValidDate;
}

bool Station::active() const { return this->m_active; }

void Station::setActive(bool active) { this->m_active = active; }

double Station::navd88Offset() const { return this->m_navd88Offset; }

void Station::setNavd88Offset(double navd88Offset) {
  this->m_navd88Offset = navd88Offset;
}

double Station::mslOffset() const { return this->m_mslOffset; }

void Station::setMslOffset(double mslOffset) { this->m_mslOffset = mslOffset; }

double Station::ngvd29Offset() const { return m_ngvd29Offset; }

void Station::setNgvd29Offset(double ngvd29Offset) {
  m_ngvd29Offset = ngvd29Offset;
}

double Station::mlwOffset() const { return this->m_mlwOffset; }

void Station::setMlwOffset(double mlwOffset) { this->m_mlwOffset = mlwOffset; }

double Station::mllwOffset() const { return this->m_mllwOffset; }

void Station::setMllwOffset(double mllwOffset) {
  this->m_mllwOffset = mllwOffset;
}

double Station::mhwOffset() const { return this->m_mhwOffset; }

void Station::setMhwOffset(double mhwOffset) { this->m_mhwOffset = mhwOffset; }

double Station::mhhwOffset() const { return this->m_mhhwOffset; }

void Station::setMhhwOffset(double mhhwOffset) {
  this->m_mhhwOffset = mhhwOffset;
}

bool Station::isNullOffset(double offset) {
  return std::abs(offset - this->nullOffset()) < 0.0001;
}
