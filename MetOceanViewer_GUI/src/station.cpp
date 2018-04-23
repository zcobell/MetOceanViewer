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
#include "station.h"

Station::Station() {
  this->m_coordinate = QGeoCoordinate();
  this->m_id = QString();
  this->m_name = QString();
  this->m_measured = 0.0;
  this->m_modeled = 0.0;
  this->m_selected = false;
}

Station::Station(QGeoCoordinate coordinate, QString id, QString name,
                 double measured, double modeled, int category) {
  this->m_coordinate = coordinate;
  this->m_id = id;
  this->m_name = name;
  this->m_measured = measured;
  this->m_modeled = modeled;
  this->m_category = category;
  this->m_selected = false;
}

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
