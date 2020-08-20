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
#include "movStation.h"

#include <cmath>
#include <limits>

MovStation::MovStation()
    : m_coordinate(QGeoCoordinate()),
      m_name("null"),
      m_id("null"),
      m_offset{0, 0, 0, 0, 0, 0, 0},
      m_modeled(0.0),
      m_measured(0.0),
      m_category(0),
      m_selected(false),
      m_active(true),
      m_startValidDate(QDateTime(QDate(1900, 1, 1), QTime(0, 0, 0))),
      m_endValidDate(QDateTime(QDate(2050, 1, 1), QTime(0, 0, 0))) {}

MovStation::MovStation(QGeoCoordinate coordinate, QString id,
                       QString name, double measured, double modeled,
                       int category, bool active, QDateTime startValidDate,
                       QDateTime endValidDate)
    : m_coordinate(coordinate),
      m_name(name),
      m_id(id),
      m_offset{0, 0, 0, 0, 0, 0},
      m_modeled(modeled),
      m_measured(measured),
      m_category(category),
      m_selected(false),
      m_active(active),
      m_startValidDate(startValidDate),
      m_endValidDate(endValidDate) {}

MovStation::~MovStation() {}

bool MovStation::operator==(const MovStation &s) {
  if (this->id() == s.id() &&
      this->coordinate().latitude() == s.coordinate().latitude() &&
      this->coordinate().longitude() == s.coordinate().longitude() &&
      this->name() == s.name()) {
    return true;
  }
  return false;
}

QGeoCoordinate MovStation::coordinate() const { return this->m_coordinate; }

void MovStation::setLatitude(const double latitude) {
  this->m_coordinate.setLatitude(latitude);
}

void MovStation::setLongitude(const double longitude) {
  this->m_coordinate.setLongitude(longitude);
}

void MovStation::setCoordinate(const QGeoCoordinate &coordinate) {
  this->m_coordinate = coordinate;
}

QString MovStation::name() const { return this->m_name; }

void MovStation::setName(const QString &name) { this->m_name = name; }

QString MovStation::id() const { return this->m_id; }

void MovStation::setId(const QString &id) { this->m_id = id; }

bool MovStation::selected() const { return this->m_selected; }

void MovStation::setSelected(bool selected) { this->m_selected = selected; }

double MovStation::modeled() const { return m_modeled; }

void MovStation::setModeled(double modeled) { m_modeled = modeled; }

double MovStation::measured() const { return m_measured; }

void MovStation::setMeasured(double measured) { m_measured = measured; }

int MovStation::category() const { return this->m_category; }

void MovStation::setCategory(int category) { this->m_category = category; }

double MovStation::difference() const {
  return this->m_measured - this->m_modeled;
}

QDateTime MovStation::startValidDate() const { return this->m_startValidDate; }

void MovStation::setStartValidDate(const QDateTime &startValidDate) {
  this->m_startValidDate = startValidDate;
}

QDateTime MovStation::endValidDate() const { return this->m_endValidDate; }

void MovStation::setEndValidDate(const QDateTime &endValidDate) {
  this->m_endValidDate = endValidDate;
}

bool MovStation::active() const { return this->m_active; }

void MovStation::setActive(bool active) { this->m_active = active; }

double MovStation::navd88Offset() const {
  return this->m_offset[Datum::NAVD88];
}

void MovStation::setNavd88Offset(double navd88Offset) {
  this->m_offset[Datum::NAVD88] = navd88Offset;
}

double MovStation::mslOffset() const { return this->m_offset[Datum::MSL]; }

void MovStation::setMslOffset(double mslOffset) {
  this->m_offset[Datum::MSL] = mslOffset;
}

double MovStation::ngvd29Offset() const { return m_offset[Datum::NGVD29]; }

void MovStation::setNgvd29Offset(double ngvd29Offset) {
  m_offset[Datum::NGVD29] = ngvd29Offset;
}

double MovStation::mlwOffset() const { return this->m_offset[Datum::MLW]; }

void MovStation::setMlwOffset(double mlwOffset) {
  this->m_offset[Datum::MLW] = mlwOffset;
}

double MovStation::mllwOffset() const { return this->m_offset[Datum::MLLW]; }

void MovStation::setMllwOffset(double mllwOffset) {
  this->m_offset[Datum::MLLW] = mllwOffset;
}

double MovStation::mhwOffset() const { return this->m_offset[Datum::MHW]; }

void MovStation::setMhwOffset(double mhwOffset) {
  this->m_offset[Datum::MHW] = mhwOffset;
}

double MovStation::mhhwOffset() const { return this->m_offset[Datum::MHHW]; }

void MovStation::setMhhwOffset(double mhhwOffset) {
  this->m_offset[Datum::MHHW] = mhhwOffset;
}

double MovStation::offset(const Datum::VDatum &d) const {
  return this->m_offset[d];
}

void MovStation::setOffset(const Datum::VDatum &d, const double value) {
  this->m_offset[d] = value;
}

bool MovStation::isNullOffset(double offset) {
  return std::abs(offset - this->nullOffset()) <
         std::numeric_limits<double>::epsilon();
}
