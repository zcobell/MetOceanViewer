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
#include "timezonestruct.h"

TimezoneStruct::TimezoneStruct() {
  this->m_abbreviation = "UTC";
  this->m_location = "Worldwide";
  this->m_name = "Universal Time Coordinated";
  this->m_offsetSeconds = 0;
}

TimezoneStruct::TimezoneStruct(TZData::Location locationCode, TZData::Abbreviation abbreviationCode,
                               QString abbreviation, QString name,
                               QString location, int offsetSeconds) {
  this->m_locationCode = locationCode;
  this->m_abbreviationCode = abbreviationCode;
  this->m_abbreviation = abbreviation;
  this->m_name = name;
  this->m_location = location;
  this->m_offsetSeconds = offsetSeconds;
}

QString TimezoneStruct::name() const { return this->m_name; }

void TimezoneStruct::setName(const QString &name) { this->m_name = name; }

QString TimezoneStruct::abbreviation() const { return this->m_abbreviation; }

void TimezoneStruct::setAbbreviation(const QString &abbreviation) {
  this->m_abbreviation = abbreviation;
}

int TimezoneStruct::getOffsetSeconds() const { return this->m_offsetSeconds; }

void TimezoneStruct::setOffsetSeconds(int value) {
  this->m_offsetSeconds = value;
}

QString TimezoneStruct::getLocation() const { return this->m_location; }

void TimezoneStruct::setLocation(const QString &location) {
  this->m_location = location;
}

TZData::Location TimezoneStruct::getLocationCode() const
{
  return m_locationCode;
}

void TimezoneStruct::setLocationCode(const TZData::Location &locationCode)
{
  m_locationCode = locationCode;
}

TZData::Abbreviation TimezoneStruct::getAbbreviationCode() const
{
  return m_abbreviationCode;
}

void TimezoneStruct::setAbbreviationCode(const TZData::Abbreviation &abbreviationCode)
{
  m_abbreviationCode = abbreviationCode;
}
