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
#include "timezone.h"

Timezone::Timezone(QObject *parent) : QObject(parent) {
  this->m_initialized = false;
  this->build();
  this->m_zone =
      this->m_timezones[std::make_pair(TZData::Worldwide, TZData::UTC)];
}

bool Timezone::fromAbbreviation(QString value, TZData::Location location) {
  //...First pass checks against the given location code
  for (QMap<std::pair<TZData::Location, TZData::Abbreviation>,
            TimezoneStruct>::iterator i = this->m_timezones.begin();
       i != this->m_timezones.end(); ++i) {
    if (i.value().abbreviation().simplified() == value.simplified() &&
        i.value().getLocationCode() == location) {
      this->m_zone = this->m_timezones[i.key()];
      this->m_initialized = true;
      return true;
    }
  }

  //...Second pass ignores location code
  for (QMap<std::pair<TZData::Location, TZData::Abbreviation>,
            TimezoneStruct>::iterator i = this->m_timezones.begin();
       i != this->m_timezones.end(); ++i) {
    if (i.value().abbreviation().simplified() == value.simplified()) {
      this->m_zone = this->m_timezones[i.key()];
      this->m_initialized = true;
      return true;
    }
  }

  return false;
}

bool Timezone::initialized() { return this->m_initialized; }

int Timezone::utcOffset() {
  if (this->m_initialized)
    return this->m_zone.getOffsetSeconds();
  else
    return 0;
}

int Timezone::offsetTo(Timezone &zone) {
  if (this->m_initialized && zone.initialized())
    return this->utcOffset() - zone.utcOffset();
  else
    return 0;
}

QString Timezone::abbreviation() {
  if (this->m_initialized) return this->m_zone.abbreviation();
  return QStringLiteral("Uninitialized");
}

QStringList Timezone::getAllTimezoneAbbreviations() {
  QStringList list;
  for (QMap<std::pair<TZData::Location, TZData::Abbreviation>,
            TimezoneStruct>::iterator i = this->m_timezones.begin();
       i != this->m_timezones.end(); ++i) {
    list.append(i.value().abbreviation());
  }
  return list;
}

QStringList Timezone::getAllTimezoneNames() {
  QStringList list;
  for (QMap<std::pair<TZData::Location, TZData::Abbreviation>,
            TimezoneStruct>::iterator i = this->m_timezones.begin();
       i != this->m_timezones.end(); ++i) {
    list.append(i.value().name());
  }
  return list;
}

QStringList Timezone::getTimezoneNames(TZData::Location location) {
  QStringList list;
  for (QMap<std::pair<TZData::Location, TZData::Abbreviation>,
            TimezoneStruct>::iterator i = this->m_timezones.begin();
       i != this->m_timezones.end(); ++i) {
    if (i.value().getLocationCode() == location) list.append(i.value().name());
  }
  return list;
}

QStringList Timezone::getTimezoneAbbreviations(TZData::Location location) {
  QStringList list;
  for (QMap<std::pair<TZData::Location, TZData::Abbreviation>,
            TimezoneStruct>::iterator i = this->m_timezones.begin();
       i != this->m_timezones.end(); ++i) {
    if (i.value().getLocationCode() == location)
      list.append(i.value().abbreviation());
  }
  return list;
}

void Timezone::build() {
  using namespace TZData;
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, A),
                           TimezoneStruct(Military, A, QStringLiteral("A"),
                                          QStringLiteral("Alpha Time Zone"),
                                          QStringLiteral("Military"), 3600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Australia, ACDT),
      TimezoneStruct(Australia, ACDT, QStringLiteral("ACDT"),
                     QStringLiteral("Australian Central Daylight Time"),
                     QStringLiteral("Australia"), 37800.0));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Australia, ACST),
      TimezoneStruct(Australia, ACST, QStringLiteral("ACST"),
                     QStringLiteral("Australian Central Standard Time"),
                     QStringLiteral("Australia"), 34200.0));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, ACT),
      TimezoneStruct(SouthAmerica, ACT, QStringLiteral("ACT"),
                     QStringLiteral("Acre Time"),
                     QStringLiteral("South America"), -18000));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Australia, ACT),
      TimezoneStruct(Australia, ACT, QStringLiteral("ACT"),
                     QStringLiteral("Australian Central Time"),
                     QStringLiteral("Australia"), 34200.0));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Australia, ACWST),
      TimezoneStruct(Australia, ACWST, QStringLiteral("ACWST"),
                     QStringLiteral("Australian Central Western Standard Time"),
                     QStringLiteral("Australia"), 31500.00));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Asia, ADT),
      TimezoneStruct(Asia, ADT, QStringLiteral("ADT"),
                     QStringLiteral("Arabia Daylight Time"),
                     QStringLiteral("Asia"), 14400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, ADT),
      TimezoneStruct(NorthAmerica, ADT, QStringLiteral("ADT"),
                     QStringLiteral("Atlantic Daylight Time"),
                     QStringLiteral("North America"), -10800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Australia, AEDT),
      TimezoneStruct(Australia, AEDT, QStringLiteral("AEDT"),
                     QStringLiteral("Australian Eastern Daylight Time"),
                     QStringLiteral("Australia"), 39600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Australia, AEST),
      TimezoneStruct(Australia, AEST, QStringLiteral("AEST"),
                     QStringLiteral("Australian Eastern Standard Time"),
                     QStringLiteral("Australia"), 36000));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Australia, AET),
      TimezoneStruct(Australia, AET, QStringLiteral("AET"),
                     QStringLiteral("Australian Eastern Time"),
                     QStringLiteral("Australia"), 36000));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, AFT),
                           TimezoneStruct(Asia, AFT, QStringLiteral("AFT"),
                                          QStringLiteral("Afghanistan Time"),
                                          QStringLiteral("Asia"), 16200.0));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, AKDT),
      TimezoneStruct(NorthAmerica, AKDT, QStringLiteral("AKDT"),
                     QStringLiteral("Alaska Daylight Time"),
                     QStringLiteral("North America"), -28800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, AKST),
      TimezoneStruct(NorthAmerica, AKST, QStringLiteral("AKST"),
                     QStringLiteral("Alaska Standard Time"),
                     QStringLiteral("North America"), -32400));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, ALMT),
                           TimezoneStruct(Asia, ALMT, QStringLiteral("ALMT"),
                                          QStringLiteral("Alma-Ata Time"),
                                          QStringLiteral("Asia"), 21600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, AMST),
      TimezoneStruct(SouthAmerica, AMST, QStringLiteral("AMST"),
                     QStringLiteral("Amazon Summer Time"),
                     QStringLiteral("South America"), -10800));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, AMST),
                           TimezoneStruct(Asia, AMST, QStringLiteral("AMST"),
                                          QStringLiteral("Armenia Summer Time"),
                                          QStringLiteral("Asia"), 18000));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, AMT),
      TimezoneStruct(SouthAmerica, AMT, QStringLiteral("AMT"),
                     QStringLiteral("Amazon Time"),
                     QStringLiteral("South America"), -14400));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, AMT),
                           TimezoneStruct(Asia, AMT, QStringLiteral("AMT"),
                                          QStringLiteral("Armenia Time"),
                                          QStringLiteral("Asia"), 14400));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, ANAST),
                           TimezoneStruct(Asia, ANAST, QStringLiteral("ANAST"),
                                          QStringLiteral("Anadyr Summer Time"),
                                          QStringLiteral("Asia"), 43200));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, ANAT),
                           TimezoneStruct(Asia, ANAT, QStringLiteral("ANAT"),
                                          QStringLiteral("Anadyr Time"),
                                          QStringLiteral("Asia"), 43200));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, AQTT),
                           TimezoneStruct(Asia, AQTT, QStringLiteral("AQTT"),
                                          QStringLiteral("Aqtobe Time"),
                                          QStringLiteral("Asia"), 18000));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Antarctica, ART),
      TimezoneStruct(Antarctica, ART, QStringLiteral("ART"),
                     QStringLiteral("Argentina Time"),
                     QStringLiteral("Antarctica"), -10800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Asia, AST),
      TimezoneStruct(Asia, AST, QStringLiteral("AST"),
                     QStringLiteral("Arabia Standard Time"),
                     QStringLiteral("Asia"), 10800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, AST),
      TimezoneStruct(NorthAmerica, AST, QStringLiteral("AST"),
                     QStringLiteral("Atlantic Standard Time"),
                     QStringLiteral("North America"), -14400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Australia, AWDT),
      TimezoneStruct(Australia, AWDT, QStringLiteral("AWDT"),
                     QStringLiteral("Australian Western Daylight Time"),
                     QStringLiteral("Australia"), 32400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Australia, AWST),
      TimezoneStruct(Australia, AWST, QStringLiteral("AWST"),
                     QStringLiteral("Australian Western Standard Time"),
                     QStringLiteral("Australia"), 28800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Atlantic, AZOST),
      TimezoneStruct(Atlantic, AZOST, QStringLiteral("AZOST"),
                     QStringLiteral("Azores Summer Time"),
                     QStringLiteral("Atlantic"), 0));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Atlantic, AZOT),
      TimezoneStruct(Atlantic, AZOT, QStringLiteral("AZOT"),
                     QStringLiteral("Azores Time"), QStringLiteral("Atlantic"),
                     -3600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Asia, AZST),
      TimezoneStruct(Asia, AZST, QStringLiteral("AZST"),
                     QStringLiteral("Azerbaijan Summer Time"),
                     QStringLiteral("Asia"), 18000));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, AZT),
                           TimezoneStruct(Asia, AZT, QStringLiteral("AZT"),
                                          QStringLiteral("Azerbaijan Time"),
                                          QStringLiteral("Asia"), 14400));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Pacific, AoE),
                           TimezoneStruct(Pacific, AoE, QStringLiteral("AoE"),
                                          QStringLiteral("Anywhere on Earth"),
                                          QStringLiteral("Pacific"), -43200));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, B),
                           TimezoneStruct(Military, B, QStringLiteral("B"),
                                          QStringLiteral("Bravo Time Zone"),
                                          QStringLiteral("Military"), 7200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Asia, BNT),
      TimezoneStruct(Asia, BNT, QStringLiteral("BNT"),
                     QStringLiteral("Brunei Darussalam Time"),
                     QStringLiteral("Asia"), 28800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, BOT),
      TimezoneStruct(SouthAmerica, BOT, QStringLiteral("BOT"),
                     QStringLiteral("Bolivia Time"),
                     QStringLiteral("South America"), -14400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, BRST),
      TimezoneStruct(SouthAmerica, BRST, QStringLiteral("BRST"),
                     QStringLiteral("Brasília Summer Time"),
                     QStringLiteral("South America"), -7200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, BRT),
      TimezoneStruct(SouthAmerica, BRT, QStringLiteral("BRT"),
                     QStringLiteral("Brasília Time"),
                     QStringLiteral("South America"), -10800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Asia, BST),
      TimezoneStruct(Asia, BST, QStringLiteral("BST"),
                     QStringLiteral("Bangladesh Standard Time"),
                     QStringLiteral("Asia"), 21600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, BST),
      TimezoneStruct(Pacific, BST, QStringLiteral("BST"),
                     QStringLiteral("Bougainville Standard Time"),
                     QStringLiteral("Pacific"), 39600));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Europe, BST),
                           TimezoneStruct(Europe, BST, QStringLiteral("BST"),
                                          QStringLiteral("British Summer Time"),
                                          QStringLiteral("Europe"), 3600));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, BTT),
                           TimezoneStruct(Asia, BTT, QStringLiteral("BTT"),
                                          QStringLiteral("Bhutan Time"),
                                          QStringLiteral("Asia"), 21600));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, C),
                           TimezoneStruct(Military, C, QStringLiteral("C"),
                                          QStringLiteral("Charlie Time Zone"),
                                          QStringLiteral("Military"), 10800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Antarctica, CAST),
      TimezoneStruct(Antarctica, CAST, QStringLiteral("CAST"),
                     QStringLiteral("Casey Time"), QStringLiteral("Antarctica"),
                     28800));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Africa, CAT),
                           TimezoneStruct(Africa, CAT, QStringLiteral("CAT"),
                                          QStringLiteral("Central Africa Time"),
                                          QStringLiteral("Africa"), 7200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(IndianOcean, CCT),
      TimezoneStruct(IndianOcean, CCT, QStringLiteral("CCT"),
                     QStringLiteral("Cocos Islands Time"),
                     QStringLiteral("Indian Ocean"), 23400.0));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, CDT),
      TimezoneStruct(NorthAmerica, CDT, QStringLiteral("CDT"),
                     QStringLiteral("Central Daylight Time"),
                     QStringLiteral("North America"), -18000));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Caribbean, CDT),
      TimezoneStruct(Caribbean, CDT, QStringLiteral("CDT"),
                     QStringLiteral("Cuba Daylight Time"),
                     QStringLiteral("Caribbean"), -14400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Europe, CEST),
      TimezoneStruct(Europe, CEST, QStringLiteral("CEST"),
                     QStringLiteral("Central European Summer Time"),
                     QStringLiteral("Europe"), 7200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Europe, CET),
      TimezoneStruct(Europe, CET, QStringLiteral("CET"),
                     QStringLiteral("Central European Time"),
                     QStringLiteral("Europe"), 3600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, CHADT),
      TimezoneStruct(Pacific, CHADT, QStringLiteral("CHADT"),
                     QStringLiteral("Chatham Island Daylight Time"),
                     QStringLiteral("Pacific"), 49500.00));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, CHAST),
      TimezoneStruct(Pacific, CHAST, QStringLiteral("CHAST"),
                     QStringLiteral("Chatham Island Standard Time"),
                     QStringLiteral("Pacific"), 45900.00));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Asia, CHOST),
      TimezoneStruct(Asia, CHOST, QStringLiteral("CHOST"),
                     QStringLiteral("Choibalsan Summer Time"),
                     QStringLiteral("Asia"), 32400));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, CHOT),
                           TimezoneStruct(Asia, CHOT, QStringLiteral("CHOT"),
                                          QStringLiteral("Choibalsan Time"),
                                          QStringLiteral("Asia"), 28800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, CHUT),
      TimezoneStruct(Pacific, CHUT, QStringLiteral("CHUT"),
                     QStringLiteral("Chuuk Time"), QStringLiteral("Pacific"),
                     36000));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Caribbean, CIDST),
      TimezoneStruct(Caribbean, CIDST, QStringLiteral("CIDST"),
                     QStringLiteral("Cayman Islands Daylight Saving Time"),
                     QStringLiteral("Caribbean"), -14400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Caribbean, CIST),
      TimezoneStruct(Caribbean, CIST, QStringLiteral("CIST"),
                     QStringLiteral("Cayman Islands Standard Time"),
                     QStringLiteral("Caribbean"), -18000));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Pacific, CKT),
                           TimezoneStruct(Pacific, CKT, QStringLiteral("CKT"),
                                          QStringLiteral("Cook Island Time"),
                                          QStringLiteral("Pacific"), -36000));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, CLST),
      TimezoneStruct(SouthAmerica, CLST, QStringLiteral("CLST"),
                     QStringLiteral("Chile Summer Time"),
                     QStringLiteral("South America"), -10800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, CLT),
      TimezoneStruct(SouthAmerica, CLT, QStringLiteral("CLT"),
                     QStringLiteral("Chile Standard Time"),
                     QStringLiteral("South America"), -14400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, COT),
      TimezoneStruct(SouthAmerica, COT, QStringLiteral("COT"),
                     QStringLiteral("Colombia Time"),
                     QStringLiteral("South America"), -18000));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, CST),
      TimezoneStruct(NorthAmerica, CST, QStringLiteral("CST"),
                     QStringLiteral("Central Standard Time"),
                     QStringLiteral("North America"), -21600));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, CST),
                           TimezoneStruct(Asia, CST, QStringLiteral("CST"),
                                          QStringLiteral("China Standard Time"),
                                          QStringLiteral("Asia"), 28800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Caribbean, CST),
      TimezoneStruct(Caribbean, CST, QStringLiteral("CST"),
                     QStringLiteral("Cuba Standard Time"),
                     QStringLiteral("Caribbean"), -18000));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Africa, CVT),
                           TimezoneStruct(Africa, CVT, QStringLiteral("CVT"),
                                          QStringLiteral("Cape Verde Time"),
                                          QStringLiteral("Africa"), -3600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Australia, CXT),
      TimezoneStruct(Australia, CXT, QStringLiteral("CXT"),
                     QStringLiteral("Christmas Island Time"),
                     QStringLiteral("Australia"), 25200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, ChST),
      TimezoneStruct(Pacific, ChST, QStringLiteral("ChST"),
                     QStringLiteral("Chamorro Standard Time"),
                     QStringLiteral("Pacific"), 36000));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, D),
                           TimezoneStruct(Military, D, QStringLiteral("D"),
                                          QStringLiteral("Delta Time Zone"),
                                          QStringLiteral("Military"), 14400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Antarctica, DAVT),
      TimezoneStruct(Antarctica, DAVT, QStringLiteral("DAVT"),
                     QStringLiteral("Davis Time"), QStringLiteral("Antarctica"),
                     25200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Antarctica, DDUT),
      TimezoneStruct(Antarctica, DDUT, QStringLiteral("DDUT"),
                     QStringLiteral(""), QStringLiteral("Antarctica"), 36000));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, E),
                           TimezoneStruct(Military, E, QStringLiteral("E"),
                                          QStringLiteral("Echo Time Zone"),
                                          QStringLiteral("Military"), 18000));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, EASST),
      TimezoneStruct(Pacific, EASST, QStringLiteral("EASST"),
                     QStringLiteral("Easter Island Summer Time"),
                     QStringLiteral("Pacific"), -18000));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, EAST),
      TimezoneStruct(Pacific, EAST, QStringLiteral("EAST"),
                     QStringLiteral("Easter Island Standard Time"),
                     QStringLiteral("Pacific"), -21600));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Africa, EAT),
                           TimezoneStruct(Africa, EAT, QStringLiteral("EAT"),
                                          QStringLiteral("Eastern Africa Time"),
                                          QStringLiteral("Africa"), 10800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, ECT),
      TimezoneStruct(SouthAmerica, ECT, QStringLiteral("ECT"),
                     QStringLiteral("Ecuador Time"),
                     QStringLiteral("South America"), -18000));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, EDT),
      TimezoneStruct(NorthAmerica, EDT, QStringLiteral("EDT"),
                     QStringLiteral("Eastern Daylight Time"),
                     QStringLiteral("North America"), -14400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Europe, EEST),
      TimezoneStruct(Europe, EEST, QStringLiteral("EEST"),
                     QStringLiteral("Eastern European Summer Time"),
                     QStringLiteral("Europe"), 10800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Europe, EET),
      TimezoneStruct(Europe, EET, QStringLiteral("EET"),
                     QStringLiteral("Eastern European Time"),
                     QStringLiteral("Europe"), 7200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, EGST),
      TimezoneStruct(NorthAmerica, EGST, QStringLiteral("EGST"),
                     QStringLiteral("Eastern Greenland Summer Time"),
                     QStringLiteral("North America"), 0));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, EGT),
      TimezoneStruct(NorthAmerica, EGT, QStringLiteral("EGT"),
                     QStringLiteral("East Greenland Time"),
                     QStringLiteral("North America"), -3600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, EST),
      TimezoneStruct(NorthAmerica, EST, QStringLiteral("EST"),
                     QStringLiteral("Eastern Standard Time"),
                     QStringLiteral("North America"), -18000));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, F),
                           TimezoneStruct(Military, F, QStringLiteral("F"),
                                          QStringLiteral("Foxtrot Time Zone"),
                                          QStringLiteral("Military"), 21600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Europe, FET),
      TimezoneStruct(Europe, FET, QStringLiteral("FET"),
                     QStringLiteral("Further-Eastern European Time"),
                     QStringLiteral("Europe"), 10800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, FJST),
      TimezoneStruct(Pacific, FJST, QStringLiteral("FJST"),
                     QStringLiteral("Fiji Summer Time"),
                     QStringLiteral("Pacific"), 46800));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Pacific, FJT),
                           TimezoneStruct(Pacific, FJT, QStringLiteral("FJT"),
                                          QStringLiteral("Fiji Time"),
                                          QStringLiteral("Pacific"), 43200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, FKST),
      TimezoneStruct(SouthAmerica, FKST, QStringLiteral("FKST"),
                     QStringLiteral("Falkland Islands Summer Time"),
                     QStringLiteral("South America"), -10800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, FKT),
      TimezoneStruct(SouthAmerica, FKT, QStringLiteral("FKT"),
                     QStringLiteral("Falkland Island Time"),
                     QStringLiteral("South America"), -14400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, FNT),
      TimezoneStruct(SouthAmerica, FNT, QStringLiteral("FNT"),
                     QStringLiteral("Fernando de Noronha Time"),
                     QStringLiteral("South America"), -7200));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, G),
                           TimezoneStruct(Military, G, QStringLiteral("G"),
                                          QStringLiteral("Golf Time Zone"),
                                          QStringLiteral("Military"), 25200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, GALT),
      TimezoneStruct(Pacific, GALT, QStringLiteral("GALT"),
                     QStringLiteral("Galapagos Time"),
                     QStringLiteral("Pacific"), -21600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, GAMT),
      TimezoneStruct(Pacific, GAMT, QStringLiteral("GAMT"),
                     QStringLiteral("Gambier Time"), QStringLiteral("Pacific"),
                     -32400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Asia, GET),
      TimezoneStruct(Asia, GET, QStringLiteral("GET"),
                     QStringLiteral("Georgia Standard Time"),
                     QStringLiteral("Asia"), 14400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, GFT),
      TimezoneStruct(SouthAmerica, GFT, QStringLiteral("GFT"),
                     QStringLiteral("French Guiana Time"),
                     QStringLiteral("South America"), -10800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, GILT),
      TimezoneStruct(Pacific, GILT, QStringLiteral("GILT"),
                     QStringLiteral("Gilbert Island Time"),
                     QStringLiteral("Pacific"), 43200));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Europe, GMT),
                           TimezoneStruct(Europe, GMT, QStringLiteral("GMT"),
                                          QStringLiteral("Greenwich Mean Time"),
                                          QStringLiteral("Europe"), 0));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, GST),
                           TimezoneStruct(Asia, GST, QStringLiteral("GST"),
                                          QStringLiteral("Gulf Standard Time"),
                                          QStringLiteral("Asia"), 14400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, GST),
      TimezoneStruct(SouthAmerica, GST, QStringLiteral("GST"),
                     QStringLiteral("South Georgia Time"),
                     QStringLiteral("South America"), -7200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, GYT),
      TimezoneStruct(SouthAmerica, GYT, QStringLiteral("GYT"),
                     QStringLiteral("Guyana Time"),
                     QStringLiteral("South America"), -14400));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, H),
                           TimezoneStruct(Military, H, QStringLiteral("H"),
                                          QStringLiteral("Hotel Time Zone"),
                                          QStringLiteral("Military"), 28800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, HADT),
      TimezoneStruct(NorthAmerica, HADT, QStringLiteral("HADT"),
                     QStringLiteral("Hawaii-Aleutian Daylight Time"),
                     QStringLiteral("North America"), -32400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, HAST),
      TimezoneStruct(NorthAmerica, HAST, QStringLiteral("HAST"),
                     QStringLiteral("Hawaii-Aleutian Standard Time"),
                     QStringLiteral("North America"), -36000));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, HKT),
                           TimezoneStruct(Asia, HKT, QStringLiteral("HKT"),
                                          QStringLiteral("Hong Kong Time"),
                                          QStringLiteral("Asia"), 28800));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, HOVST),
                           TimezoneStruct(Asia, HOVST, QStringLiteral("HOVST"),
                                          QStringLiteral("Hovd Summer Time"),
                                          QStringLiteral("Asia"), 28800));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, HOVT),
                           TimezoneStruct(Asia, HOVT, QStringLiteral("HOVT"),
                                          QStringLiteral("Hovd Time"),
                                          QStringLiteral("Asia"), 25200));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, I),
                           TimezoneStruct(Military, I, QStringLiteral("I"),
                                          QStringLiteral("India Time Zone"),
                                          QStringLiteral("Military"), 32400));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, ICT),
                           TimezoneStruct(Asia, ICT, QStringLiteral("ICT"),
                                          QStringLiteral("Indochina Time"),
                                          QStringLiteral("Asia"), 25200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Asia, IDT),
      TimezoneStruct(Asia, IDT, QStringLiteral("IDT"),
                     QStringLiteral("Israel Daylight Time"),
                     QStringLiteral("Asia"), 10800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(IndianOcean, IOT),
      TimezoneStruct(IndianOcean, IOT, QStringLiteral("IOT"),
                     QStringLiteral("Indian Chagos Time"),
                     QStringLiteral("Indian Ocean"), 21600));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, IRDT),
                           TimezoneStruct(Asia, IRDT, QStringLiteral("IRDT"),
                                          QStringLiteral("Iran Daylight Time"),
                                          QStringLiteral("Asia"), 16200.0));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, IRKST),
                           TimezoneStruct(Asia, IRKST, QStringLiteral("IRKST"),
                                          QStringLiteral("Irkutsk Summer Time"),
                                          QStringLiteral("Asia"), 32400));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, IRKT),
                           TimezoneStruct(Asia, IRKT, QStringLiteral("IRKT"),
                                          QStringLiteral("Irkutsk Time"),
                                          QStringLiteral("Asia"), 28800));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, IRST),
                           TimezoneStruct(Asia, IRST, QStringLiteral("IRST"),
                                          QStringLiteral("Iran Standard Time"),
                                          QStringLiteral("Asia"), 12600.0));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, IST),
                           TimezoneStruct(Asia, IST, QStringLiteral("IST"),
                                          QStringLiteral("India Standard Time"),
                                          QStringLiteral("Asia"), 19800.0));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Europe, IST),
                           TimezoneStruct(Europe, IST, QStringLiteral("IST"),
                                          QStringLiteral("Irish Standard Time"),
                                          QStringLiteral("Europe"), 3600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Asia, IST),
      TimezoneStruct(Asia, IST, QStringLiteral("IST"),
                     QStringLiteral("Israel Standard Time"),
                     QStringLiteral("Asia"), 7200));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, JST),
                           TimezoneStruct(Asia, JST, QStringLiteral("JST"),
                                          QStringLiteral("Japan Standard Time"),
                                          QStringLiteral("Asia"), 32400));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, K),
                           TimezoneStruct(Military, K, QStringLiteral("K"),
                                          QStringLiteral("Kilo Time Zone"),
                                          QStringLiteral("Military"), 36000));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, KGT),
                           TimezoneStruct(Asia, KGT, QStringLiteral("KGT"),
                                          QStringLiteral("Kyrgyzstan Time"),
                                          QStringLiteral("Asia"), 21600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, KOST),
      TimezoneStruct(Pacific, KOST, QStringLiteral("KOST"),
                     QStringLiteral("Kosrae Time"), QStringLiteral("Pacific"),
                     39600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Asia, KRAST),
      TimezoneStruct(Asia, KRAST, QStringLiteral("KRAST"),
                     QStringLiteral("Krasnoyarsk Summer Time"),
                     QStringLiteral("Asia"), 28800));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, KRAT),
                           TimezoneStruct(Asia, KRAT, QStringLiteral("KRAT"),
                                          QStringLiteral("Krasnoyarsk Time"),
                                          QStringLiteral("Asia"), 25200));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, KST),
                           TimezoneStruct(Asia, KST, QStringLiteral("KST"),
                                          QStringLiteral("Korea Standard Time"),
                                          QStringLiteral("Asia"), 32400));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Europe, KUYT),
                           TimezoneStruct(Europe, KUYT, QStringLiteral("KUYT"),
                                          QStringLiteral("Kuybyshev Time"),
                                          QStringLiteral("Europe"), 14400));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, L),
                           TimezoneStruct(Military, L, QStringLiteral("L"),
                                          QStringLiteral("Lima Time Zone"),
                                          QStringLiteral("Military"), 39600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Australia, LHDT),
      TimezoneStruct(Australia, LHDT, QStringLiteral("LHDT"),
                     QStringLiteral("Lord Howe Daylight Time"),
                     QStringLiteral("Australia"), 39600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Australia, LHST),
      TimezoneStruct(Australia, LHST, QStringLiteral("LHST"),
                     QStringLiteral("Lord Howe Standard Time"),
                     QStringLiteral("Australia"), 37800.0));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, LINT),
      TimezoneStruct(Pacific, LINT, QStringLiteral("LINT"),
                     QStringLiteral("Line Islands Time"),
                     QStringLiteral("Pacific"), 50400));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, M),
                           TimezoneStruct(Military, M, QStringLiteral("M"),
                                          QStringLiteral("Mike Time Zone"),
                                          QStringLiteral("Military"), 43200));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, MAGST),
                           TimezoneStruct(Asia, MAGST, QStringLiteral("MAGST"),
                                          QStringLiteral("Magadan Summer Time"),
                                          QStringLiteral("Asia"), 43200));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, MAGT),
                           TimezoneStruct(Asia, MAGT, QStringLiteral("MAGT"),
                                          QStringLiteral("Magadan Time"),
                                          QStringLiteral("Asia"), 39600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, MART),
      TimezoneStruct(Pacific, MART, QStringLiteral("MART"),
                     QStringLiteral("Marquesas Time"),
                     QStringLiteral("Pacific"), -34200.0));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Antarctica, MAWT),
      TimezoneStruct(Antarctica, MAWT, QStringLiteral("MAWT"),
                     QStringLiteral("Mawson Time"),
                     QStringLiteral("Antarctica"), 18000));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, MDT),
      TimezoneStruct(NorthAmerica, MDT, QStringLiteral("MDT"),
                     QStringLiteral("Mountain Daylight Time"),
                     QStringLiteral("North America"), -21600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, MHT),
      TimezoneStruct(Pacific, MHT, QStringLiteral("MHT"),
                     QStringLiteral("Marshall Islands Time"),
                     QStringLiteral("Pacific"), 43200));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, MMT),
                           TimezoneStruct(Asia, MMT, QStringLiteral("MMT"),
                                          QStringLiteral("Myanmar Time"),
                                          QStringLiteral("Asia"), 23400.0));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Europe, MSD),
      TimezoneStruct(Europe, MSD, QStringLiteral("MSD"),
                     QStringLiteral("Moscow Daylight Time"),
                     QStringLiteral("Europe"), 14400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Europe, MSK),
      TimezoneStruct(Europe, MSK, QStringLiteral("MSK"),
                     QStringLiteral("Moscow Standard Time"),
                     QStringLiteral("Europe"), 10800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, MST),
      TimezoneStruct(NorthAmerica, MST, QStringLiteral("MST"),
                     QStringLiteral("Mountain Standard Time"),
                     QStringLiteral("North America"), -25200));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Africa, MUT),
                           TimezoneStruct(Africa, MUT, QStringLiteral("MUT"),
                                          QStringLiteral("Mauritius Time"),
                                          QStringLiteral("Africa"), 14400));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, MVT),
                           TimezoneStruct(Asia, MVT, QStringLiteral("MVT"),
                                          QStringLiteral("Maldives Time"),
                                          QStringLiteral("Asia"), 18000));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, MYT),
                           TimezoneStruct(Asia, MYT, QStringLiteral("MYT"),
                                          QStringLiteral("Malaysia Time"),
                                          QStringLiteral("Asia"), 28800));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, N),
                           TimezoneStruct(Military, N, QStringLiteral("N"),
                                          QStringLiteral("November Time Zone"),
                                          QStringLiteral("Military"), -3600));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Pacific, NCT),
                           TimezoneStruct(Pacific, NCT, QStringLiteral("NCT"),
                                          QStringLiteral("New Caledonia Time"),
                                          QStringLiteral("Pacific"), 39600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, NDT),
      TimezoneStruct(NorthAmerica, NDT, QStringLiteral("NDT"),
                     QStringLiteral("Newfoundland Daylight Time"),
                     QStringLiteral("North America"), -9000.0));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Australia, NFT),
      TimezoneStruct(Australia, NFT, QStringLiteral("NFT"),
                     QStringLiteral("Norfolk Time"),
                     QStringLiteral("Australia"), 39600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Asia, NOVST),
      TimezoneStruct(Asia, NOVST, QStringLiteral("NOVST"),
                     QStringLiteral("Novosibirsk Summer Time"),
                     QStringLiteral("Asia"), 25200));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, NOVT),
                           TimezoneStruct(Asia, NOVT, QStringLiteral("NOVT"),
                                          QStringLiteral("Novosibirsk Time"),
                                          QStringLiteral("Asia"), 21600));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, NPT),
                           TimezoneStruct(Asia, NPT, QStringLiteral("NPT"),
                                          QStringLiteral("Nepal Time"),
                                          QStringLiteral("Asia"), 20700.00));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Pacific, NRT),
                           TimezoneStruct(Pacific, NRT, QStringLiteral("NRT"),
                                          QStringLiteral("Nauru Time"),
                                          QStringLiteral("Pacific"), 43200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, NST),
      TimezoneStruct(NorthAmerica, NST, QStringLiteral("NST"),
                     QStringLiteral("Newfoundland Standard Time"),
                     QStringLiteral("North America"), -12600.0));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Pacific, NUT),
                           TimezoneStruct(Pacific, NUT, QStringLiteral("NUT"),
                                          QStringLiteral("Niue Time"),
                                          QStringLiteral("Pacific"), -39600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, NZDT),
      TimezoneStruct(Pacific, NZDT, QStringLiteral("NZDT"),
                     QStringLiteral("New Zealand Daylight Time"),
                     QStringLiteral("Pacific"), 46800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, NZST),
      TimezoneStruct(Pacific, NZST, QStringLiteral("NZST"),
                     QStringLiteral("New Zealand Standard Time"),
                     QStringLiteral("Pacific"), 43200));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, O),
                           TimezoneStruct(Military, O, QStringLiteral("O"),
                                          QStringLiteral("Oscar Time Zone"),
                                          QStringLiteral("Military"), -7200));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, OMSST),
                           TimezoneStruct(Asia, OMSST, QStringLiteral("OMSST"),
                                          QStringLiteral("Omsk Summer Time"),
                                          QStringLiteral("Asia"), 25200));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, OMST),
                           TimezoneStruct(Asia, OMST, QStringLiteral("OMST"),
                                          QStringLiteral("Omsk Standard Time"),
                                          QStringLiteral("Asia"), 21600));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, ORAT),
                           TimezoneStruct(Asia, ORAT, QStringLiteral("ORAT"),
                                          QStringLiteral("Oral Time"),
                                          QStringLiteral("Asia"), 18000));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, P),
                           TimezoneStruct(Military, P, QStringLiteral("P"),
                                          QStringLiteral("Papa Time Zone"),
                                          QStringLiteral("Military"), -10800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, PDT),
      TimezoneStruct(NorthAmerica, PDT, QStringLiteral("PDT"),
                     QStringLiteral("Pacific Daylight Time"),
                     QStringLiteral("North America"), -25200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, PET),
      TimezoneStruct(SouthAmerica, PET, QStringLiteral("PET"),
                     QStringLiteral("Peru Time"),
                     QStringLiteral("South America"), -18000));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Asia, PETST),
      TimezoneStruct(Asia, PETST, QStringLiteral("PETST"),
                     QStringLiteral("Kamchatka Summer Time"),
                     QStringLiteral("Asia"), 43200));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, PETT),
                           TimezoneStruct(Asia, PETT, QStringLiteral("PETT"),
                                          QStringLiteral("Kamchatka Time"),
                                          QStringLiteral("Asia"), 43200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, PGT),
      TimezoneStruct(Pacific, PGT, QStringLiteral("PGT"),
                     QStringLiteral("Papua New Guinea Time"),
                     QStringLiteral("Pacific"), 36000));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, PHOT),
      TimezoneStruct(Pacific, PHOT, QStringLiteral("PHOT"),
                     QStringLiteral("Phoenix Island Time"),
                     QStringLiteral("Pacific"), 46800));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, PHT),
                           TimezoneStruct(Asia, PHT, QStringLiteral("PHT"),
                                          QStringLiteral("Philippine Time"),
                                          QStringLiteral("Asia"), 28800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Asia, PKT),
      TimezoneStruct(Asia, PKT, QStringLiteral("PKT"),
                     QStringLiteral("Pakistan Standard Time"),
                     QStringLiteral("Asia"), 18000));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, PMDT),
      TimezoneStruct(NorthAmerica, PMDT, QStringLiteral("PMDT"),
                     QStringLiteral("Pierre & Miquelon Daylight Time"),
                     QStringLiteral("North America"), -7200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, PMST),
      TimezoneStruct(NorthAmerica, PMST, QStringLiteral("PMST"),
                     QStringLiteral("Pierre & Miquelon Standard Time"),
                     QStringLiteral("North America"), -10800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, PONT),
      TimezoneStruct(Pacific, PONT, QStringLiteral("PONT"),
                     QStringLiteral("Pohnpei Standard Time"),
                     QStringLiteral("Pacific"), 39600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, PST),
      TimezoneStruct(NorthAmerica, PST, QStringLiteral("PST"),
                     QStringLiteral("Pacific Standard Time"),
                     QStringLiteral("North America"), -28800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, PST),
      TimezoneStruct(Pacific, PST, QStringLiteral("PST"),
                     QStringLiteral("Pitcairn Standard Time"),
                     QStringLiteral("Pacific"), -28800));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Pacific, PWT),
                           TimezoneStruct(Pacific, PWT, QStringLiteral("PWT"),
                                          QStringLiteral("Palau Time"),
                                          QStringLiteral("Pacific"), 32400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, PYST),
      TimezoneStruct(SouthAmerica, PYST, QStringLiteral("PYST"),
                     QStringLiteral("Paraguay Summer Time"),
                     QStringLiteral("South America"), -10800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, PYT),
      TimezoneStruct(SouthAmerica, PYT, QStringLiteral("PYT"),
                     QStringLiteral("Paraguay Time"),
                     QStringLiteral("South America"), -14400));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, PYT),
                           TimezoneStruct(Asia, PYT, QStringLiteral("PYT"),
                                          QStringLiteral("Pyongyang Time"),
                                          QStringLiteral("Asia"), 30600.0));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, Q),
                           TimezoneStruct(Military, Q, QStringLiteral("Q"),
                                          QStringLiteral("Quebec Time Zone"),
                                          QStringLiteral("Military"), -14400));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, QYZT),
                           TimezoneStruct(Asia, QYZT, QStringLiteral("QYZT"),
                                          QStringLiteral("Qyzylorda Time"),
                                          QStringLiteral("Asia"), 21600));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, R),
                           TimezoneStruct(Military, R, QStringLiteral("R"),
                                          QStringLiteral("Romeo Time Zone"),
                                          QStringLiteral("Military"), -18000));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Africa, RET),
                           TimezoneStruct(Africa, RET, QStringLiteral("RET"),
                                          QStringLiteral("Reunion Time"),
                                          QStringLiteral("Africa"), 14400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Antarctica, ROTT),
      TimezoneStruct(Antarctica, ROTT, QStringLiteral("ROTT"),
                     QStringLiteral("Rothera Time"),
                     QStringLiteral("Antarctica"), -10800));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, S),
                           TimezoneStruct(Military, S, QStringLiteral("S"),
                                          QStringLiteral("Sierra Time Zone"),
                                          QStringLiteral("Military"), -21600));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, SAKT),
                           TimezoneStruct(Asia, SAKT, QStringLiteral("SAKT"),
                                          QStringLiteral("Sakhalin Time"),
                                          QStringLiteral("Asia"), 39600));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Europe, SAMT),
                           TimezoneStruct(Europe, SAMT, QStringLiteral("SAMT"),
                                          QStringLiteral("Samara Time"),
                                          QStringLiteral("Europe"), 14400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Africa, SAST),
      TimezoneStruct(Africa, SAST, QStringLiteral("SAST"),
                     QStringLiteral("South Africa Standard Time"),
                     QStringLiteral("Africa"), 7200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, SBT),
      TimezoneStruct(Pacific, SBT, QStringLiteral("SBT"),
                     QStringLiteral("Solomon Islands Time"),
                     QStringLiteral("Pacific"), 39600));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Africa, SCT),
                           TimezoneStruct(Africa, SCT, QStringLiteral("SCT"),
                                          QStringLiteral("Seychelles Time"),
                                          QStringLiteral("Africa"), 14400));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, SGT),
                           TimezoneStruct(Asia, SGT, QStringLiteral("SGT"),
                                          QStringLiteral("Singapore Time"),
                                          QStringLiteral("Asia"), 28800));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, SRET),
                           TimezoneStruct(Asia, SRET, QStringLiteral("SRET"),
                                          QStringLiteral("Srednekolymsk Time"),
                                          QStringLiteral("Asia"), 39600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, SRT),
      TimezoneStruct(SouthAmerica, SRT, QStringLiteral("SRT"),
                     QStringLiteral("Suriname Time"),
                     QStringLiteral("South America"), -10800));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Pacific, SST),
                           TimezoneStruct(Pacific, SST, QStringLiteral("SST"),
                                          QStringLiteral("Samoa Standard Time"),
                                          QStringLiteral("Pacific"), -39600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Antarctica, SYOT),
      TimezoneStruct(Antarctica, SYOT, QStringLiteral("SYOT"),
                     QStringLiteral("Syowa Time"), QStringLiteral("Antarctica"),
                     10800));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, T),
                           TimezoneStruct(Military, T, QStringLiteral("T"),
                                          QStringLiteral("Tango Time Zone"),
                                          QStringLiteral("Military"), -25200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, TAHT),
      TimezoneStruct(Pacific, TAHT, QStringLiteral("TAHT"),
                     QStringLiteral("Tahiti Time"), QStringLiteral("Pacific"),
                     -36000));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(IndianOcean, TFT),
      TimezoneStruct(IndianOcean, TFT, QStringLiteral("TFT"),
                     QStringLiteral("French Southern and Antarctic Time"),
                     QStringLiteral("Indian Ocean"), 18000));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, TJT),
                           TimezoneStruct(Asia, TJT, QStringLiteral("TJT"),
                                          QStringLiteral("Tajikistan Time"),
                                          QStringLiteral("Asia"), 18000));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Pacific, TKT),
                           TimezoneStruct(Pacific, TKT, QStringLiteral("TKT"),
                                          QStringLiteral("Tokelau Time"),
                                          QStringLiteral("Pacific"), 46800));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, TLT),
                           TimezoneStruct(Asia, TLT, QStringLiteral("TLT"),
                                          QStringLiteral("East Timor Time"),
                                          QStringLiteral("Asia"), 32400));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, TMT),
                           TimezoneStruct(Asia, TMT, QStringLiteral("TMT"),
                                          QStringLiteral("Turkmenistan Time"),
                                          QStringLiteral("Asia"), 18000));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, TOST),
      TimezoneStruct(Pacific, TOST, QStringLiteral("TOST"),
                     QStringLiteral("Tonga Summer Time"),
                     QStringLiteral("Pacific"), 50400));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Pacific, TOT),
                           TimezoneStruct(Pacific, TOT, QStringLiteral("TOT"),
                                          QStringLiteral("Tonga Time"),
                                          QStringLiteral("Pacific"), 46800));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, TRT),
                           TimezoneStruct(Asia, TRT, QStringLiteral("TRT"),
                                          QStringLiteral("Turkey Time"),
                                          QStringLiteral("Asia"), 10800));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Pacific, TVT),
                           TimezoneStruct(Pacific, TVT, QStringLiteral("TVT"),
                                          QStringLiteral("Tuvalu Time"),
                                          QStringLiteral("Pacific"), 43200));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, U),
                           TimezoneStruct(Military, U, QStringLiteral("U"),
                                          QStringLiteral("Uniform Time Zone"),
                                          QStringLiteral("Military"), -28800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Asia, ULAST),
      TimezoneStruct(Asia, ULAST, QStringLiteral("ULAST"),
                     QStringLiteral("Ulaanbaatar Summer Time"),
                     QStringLiteral("Asia"), 32400));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, ULAT),
                           TimezoneStruct(Asia, ULAT, QStringLiteral("ULAT"),
                                          QStringLiteral("Ulaanbaatar Time"),
                                          QStringLiteral("Asia"), 28800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Worldwide, UTC),
      TimezoneStruct(Worldwide, UTC, QStringLiteral("UTC"),
                     QStringLiteral("Coordinated Universal Time"),
                     QStringLiteral("Worldwide"), 0));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, UYST),
      TimezoneStruct(SouthAmerica, UYST, QStringLiteral("UYST"),
                     QStringLiteral("Uruguay Summer Time"),
                     QStringLiteral("South America"), -7200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, UYT),
      TimezoneStruct(SouthAmerica, UYT, QStringLiteral("UYT"),
                     QStringLiteral("Uruguay Time"),
                     QStringLiteral("South America"), -10800));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, UZT),
                           TimezoneStruct(Asia, UZT, QStringLiteral("UZT"),
                                          QStringLiteral("Uzbekistan Time"),
                                          QStringLiteral("Asia"), 18000));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, V),
                           TimezoneStruct(Military, V, QStringLiteral("V"),
                                          QStringLiteral("Victor Time Zone"),
                                          QStringLiteral("Military"), -32400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, VET),
      TimezoneStruct(SouthAmerica, VET, QStringLiteral("VET"),
                     QStringLiteral("Venezuelan Standard Time"),
                     QStringLiteral("South America"), -14400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Asia, VLAST),
      TimezoneStruct(Asia, VLAST, QStringLiteral("VLAST"),
                     QStringLiteral("Vladivostok Summer Time"),
                     QStringLiteral("Asia"), 39600));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, VLAT),
                           TimezoneStruct(Asia, VLAT, QStringLiteral("VLAT"),
                                          QStringLiteral("Vladivostok Time"),
                                          QStringLiteral("Asia"), 36000));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Antarctica, VOST),
      TimezoneStruct(Antarctica, VOST, QStringLiteral("VOST"),
                     QStringLiteral("Vostok Time"),
                     QStringLiteral("Antarctica"), 21600));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Pacific, VUT),
                           TimezoneStruct(Pacific, VUT, QStringLiteral("VUT"),
                                          QStringLiteral("Vanuatu Time"),
                                          QStringLiteral("Pacific"), 39600));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, W),
                           TimezoneStruct(Military, W, QStringLiteral("W"),
                                          QStringLiteral("Whiskey Time Zone"),
                                          QStringLiteral("Military"), -36000));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, WAKT),
      TimezoneStruct(Pacific, WAKT, QStringLiteral("WAKT"),
                     QStringLiteral("Wake Time"), QStringLiteral("Pacific"),
                     43200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(SouthAmerica, WARST),
      TimezoneStruct(SouthAmerica, WARST, QStringLiteral("WARST"),
                     QStringLiteral("Western Argentine Summer Time"),
                     QStringLiteral("South America"), -10800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Africa, WAST),
      TimezoneStruct(Africa, WAST, QStringLiteral("WAST"),
                     QStringLiteral("West Africa Summer Time"),
                     QStringLiteral("Africa"), 7200));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Africa, WAT),
                           TimezoneStruct(Africa, WAT, QStringLiteral("WAT"),
                                          QStringLiteral("West Africa Time"),
                                          QStringLiteral("Africa"), 3600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Europe, WEST),
      TimezoneStruct(Europe, WEST, QStringLiteral("WEST"),
                     QStringLiteral("Western European Summer Time"),
                     QStringLiteral("Europe"), 3600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Europe, WET),
      TimezoneStruct(Europe, WET, QStringLiteral("WET"),
                     QStringLiteral("Western European Time"),
                     QStringLiteral("Europe"), 0));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, WFT),
      TimezoneStruct(Pacific, WFT, QStringLiteral("WFT"),
                     QStringLiteral("Wallis and Futuna Time"),
                     QStringLiteral("Pacific"), 43200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, WGST),
      TimezoneStruct(NorthAmerica, WGST, QStringLiteral("WGST"),
                     QStringLiteral("Western Greenland Summer Time"),
                     QStringLiteral("North America"), -7200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(NorthAmerica, WGT),
      TimezoneStruct(NorthAmerica, WGT, QStringLiteral("WGT"),
                     QStringLiteral("West Greenland Time"),
                     QStringLiteral("North America"), -10800));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Asia, WIB),
      TimezoneStruct(Asia, WIB, QStringLiteral("WIB"),
                     QStringLiteral("Western Indonesian Time"),
                     QStringLiteral("Asia"), 25200));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Asia, WIT),
      TimezoneStruct(Asia, WIT, QStringLiteral("WIT"),
                     QStringLiteral("Eastern Indonesian Time"),
                     QStringLiteral("Asia"), 32400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Asia, WITA),
      TimezoneStruct(Asia, WITA, QStringLiteral("WITA"),
                     QStringLiteral("Central Indonesian Time"),
                     QStringLiteral("Asia"), 28800));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Pacific, WST),
                           TimezoneStruct(Pacific, WST, QStringLiteral("WST"),
                                          QStringLiteral("West Samoa Time"),
                                          QStringLiteral("Pacific"), 50400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Africa, WST),
      TimezoneStruct(Africa, WST, QStringLiteral("WST"),
                     QStringLiteral("Western Sahara Summer Time"),
                     QStringLiteral("Africa"), 3600));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Africa, WT),
      TimezoneStruct(Africa, WT, QStringLiteral("WT"),
                     QStringLiteral("Western Sahara Standard Time"),
                     QStringLiteral("Africa"), 0));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, X),
                           TimezoneStruct(Military, X, QStringLiteral("X"),
                                          QStringLiteral("X-ray Time Zone"),
                                          QStringLiteral("Military"), -39600));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, Y),
                           TimezoneStruct(Military, Y, QStringLiteral("Y"),
                                          QStringLiteral("Yankee Time Zone"),
                                          QStringLiteral("Military"), -43200));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, YAKST),
                           TimezoneStruct(Asia, YAKST, QStringLiteral("YAKST"),
                                          QStringLiteral("Yakutsk Summer Time"),
                                          QStringLiteral("Asia"), 36000));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, YAKT),
                           TimezoneStruct(Asia, YAKT, QStringLiteral("YAKT"),
                                          QStringLiteral("Yakutsk Time"),
                                          QStringLiteral("Asia"), 32400));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Pacific, YAPT),
      TimezoneStruct(Pacific, YAPT, QStringLiteral("YAPT"),
                     QStringLiteral("Yap Time"), QStringLiteral("Pacific"),
                     36000));
  this->m_timezones.insert(
      std::make_pair<Location, Abbreviation>(Asia, YEKST),
      TimezoneStruct(Asia, YEKST, QStringLiteral("YEKST"),
                     QStringLiteral("Yekaterinburg Summer Time"),
                     QStringLiteral("Asia"), 21600));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Asia, YEKT),
                           TimezoneStruct(Asia, YEKT, QStringLiteral("YEKT"),
                                          QStringLiteral("Yekaterinburg Time"),
                                          QStringLiteral("Asia"), 18000));
  this->m_timezones.insert(std::make_pair<Location, Abbreviation>(Military, Z),
                           TimezoneStruct(Military, Z, QStringLiteral("Z"),
                                          QStringLiteral("Zulu Time Zone"),
                                          QStringLiteral("Military"), 0));
  return;
}
