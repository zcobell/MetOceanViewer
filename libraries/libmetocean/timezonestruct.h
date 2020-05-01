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
#ifndef TIMEZONESTRUCT_H
#define TIMEZONESTRUCT_H

#include <QString>
#include "metocean_global.h"
#include "tzdata.h"

class TimezoneStruct {
 public:
  explicit METOCEANSHARED_EXPORT TimezoneStruct();
  explicit METOCEANSHARED_EXPORT TimezoneStruct(
      TZData::Location locationCode, TZData::Abbreviation abbreviationCode,
      QString abbreviation, QString name, QString location, int offsetSeconds);

  QString METOCEANSHARED_EXPORT name() const;
  void METOCEANSHARED_EXPORT setName(const QString &name);

  QString METOCEANSHARED_EXPORT abbreviation() const;
  void METOCEANSHARED_EXPORT setAbbreviation(const QString &abbreviation);

  int METOCEANSHARED_EXPORT getOffsetSeconds() const;
  void METOCEANSHARED_EXPORT setOffsetSeconds(int value);

  QString METOCEANSHARED_EXPORT getLocation() const;
  void METOCEANSHARED_EXPORT setLocation(const QString &location);

  TZData::Location METOCEANSHARED_EXPORT getLocationCode() const;
  void METOCEANSHARED_EXPORT
  setLocationCode(const TZData::Location &locationCode);

  TZData::Abbreviation METOCEANSHARED_EXPORT getAbbreviationCode() const;
  void METOCEANSHARED_EXPORT
  setAbbreviationCode(const TZData::Abbreviation &abbreviationCode);

 private:
  TZData::Location m_locationCode;
  TZData::Abbreviation m_abbreviationCode;
  QString m_location;
  QString m_name;
  QString m_abbreviation;
  int m_offsetSeconds;
};

#endif  // TIMEZONESTRUCT_H
