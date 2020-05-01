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
#ifndef TIMEZONE_H
#define TIMEZONE_H

#include <QMap>
#include <QObject>
#include "metocean_global.h"
#include "timezonestruct.h"

class Timezone : public QObject {
  Q_OBJECT

 public:
  explicit METOCEANSHARED_EXPORT Timezone(QObject *parent = nullptr);

  METOCEANSHARED_EXPORT ~Timezone();

  static int METOCEANSHARED_EXPORT localMachineOffsetFromUtc();

  static int METOCEANSHARED_EXPORT offsetFromUtc(
      QString value, TZData::Location location = TZData::NorthAmerica);

  bool METOCEANSHARED_EXPORT fromAbbreviation(
      QString value, TZData::Location location = TZData::NorthAmerica);

  bool METOCEANSHARED_EXPORT initialized();

  int METOCEANSHARED_EXPORT utcOffset();

  int METOCEANSHARED_EXPORT offsetTo(Timezone &zone);

  QString METOCEANSHARED_EXPORT abbreviation();

  QStringList METOCEANSHARED_EXPORT getAllTimezoneAbbreviations();
  QStringList METOCEANSHARED_EXPORT getAllTimezoneNames();
  QStringList METOCEANSHARED_EXPORT
  getTimezoneAbbreviations(TZData::Location location);
  QStringList METOCEANSHARED_EXPORT getTimezoneNames(TZData::Location location);

 private:
  void build();

  bool m_initialized;

  TimezoneStruct m_zone;

  QMap<std::pair<TZData::Location, TZData::Abbreviation>, TimezoneStruct>
      m_timezones;
};

#endif  // TIMEZONE_H
