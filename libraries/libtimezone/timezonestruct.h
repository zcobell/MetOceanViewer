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
#ifndef TIMEZONESTRUCT_H
#define TIMEZONESTRUCT_H

#include <QString>
#include "tzdata.h"

class TimezoneStruct {
 public:
  explicit TimezoneStruct();
  explicit TimezoneStruct(TZData::Location locationCode,
                          TZData::Abbreviation abbreviationCode,
                          QString abbreviation, QString name, QString location,
                          int offsetSeconds);

  QString name() const;
  void setName(const QString &name);

  QString abbreviation() const;
  void setAbbreviation(const QString &abbreviation);

  int getOffsetSeconds() const;
  void setOffsetSeconds(int value);

  QString getLocation() const;
  void setLocation(const QString &location);

  TZData::Location getLocationCode() const;
  void setLocationCode(const TZData::Location &locationCode);

  TZData::Abbreviation getAbbreviationCode() const;
  void setAbbreviationCode(const TZData::Abbreviation &abbreviationCode);

 private:
  TZData::Location m_locationCode;
  TZData::Abbreviation m_abbreviationCode;
  QString m_location;
  QString m_name;
  QString m_abbreviation;
  int m_offsetSeconds;
};

#endif  // TIMEZONESTRUCT_H
