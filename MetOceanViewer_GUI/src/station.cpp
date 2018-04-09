/*-------------------------------GPL-------------------------------------//
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
#include "station.h"

Station::Station() {
  this->m_coordinate = QGeoCoordinate();
  this->m_id = QString();
  this->m_name = QString();
}

Station::Station(QGeoCoordinate coordinate, QString id, QString name) {
  this->m_coordinate = coordinate;
  this->m_id = id;
  this->m_name = name;
}

Station::~Station() {}

QGeoCoordinate Station::coordinate() const { return this->m_coordinate; }

void Station::setCoordinate(const QGeoCoordinate &coordinate) {
  this->m_coordinate = coordinate;
}

QString Station::name() const { return this->m_name; }

void Station::setName(const QString &name) { this->m_name = name; }

QString Station::id() const { return this->m_id; }

void Station::setId(const QString &id) { this->m_id = id; }
