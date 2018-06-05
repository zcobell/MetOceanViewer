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
#include "coordinate.h"

Coordinate::Coordinate() {
  this->m_x = 0.0;
  this->m_y = 0.0;
}

double Coordinate::x() const { return this->m_x; }

void Coordinate::setX(double x) { this->m_x = x; }

double Coordinate::y() const { return this->m_y; }

void Coordinate::setY(double y) { this->m_y = y; }

double Coordinate::latitude() const { return this->x(); }

void Coordinate::setLatitude(double latitude) { this->setX(latitude); }

double Coordinate::longitude() const { return this->y(); }

void Coordinate::setLongitude(double longitude) { this->setY(longitude); }
