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
#include "hmdf.h"
#include <assert.h>
#include <QFile>
#include <fstream>
#include "hmdfasciiparser.h"
#include "netcdftimeseries.h"
#include "stringutil.h"

Hmdf::Hmdf(QObject *parent) : QObject(parent) {
  this->setHeader1("");
  this->setHeader2("");
  this->setHeader3("");
  this->setDatum("");
  this->setNstations(0);
  this->setSuccess(false);
  this->setUnits("");
  // this->m_tz = Timezone(this);
}

void Hmdf::clear() {
  for (int i = 0; i < this->m_station.size(); i++) {
    this->m_station[i]->clear();
  }
  this->setHeader1("");
  this->setHeader2("");
  this->setHeader3("");
  this->setDatum("");
  this->setNstations(0);
  this->setSuccess(false);
  this->setUnits("");
  return;
}

size_t Hmdf::nstations() const { return this->m_station.size(); }

void Hmdf::setNstations(size_t nstations) {
  this->m_station.resize(nstations);
  return;
}

QString Hmdf::header1() const { return this->m_header1; }

void Hmdf::setHeader1(const QString &header1) { this->m_header1 = header1; }

QString Hmdf::header2() const { return this->m_header2; }

void Hmdf::setHeader2(const QString &header2) { this->m_header2 = header2; }

QString Hmdf::header3() const { return this->m_header3; }

void Hmdf::setHeader3(const QString &header3) { this->m_header3 = header3; }

QString Hmdf::units() const { return this->m_units; }

void Hmdf::setUnits(const QString &units) { this->m_units = units; }

QString Hmdf::datum() const { return this->m_datum; }

void Hmdf::setDatum(const QString &datum) { this->m_datum = datum; }

HmdfStation *Hmdf::station(int index) {
  assert(index >= 0 && index < this->m_station.size());
  return this->m_station[index];
}

void Hmdf::setStation(int index, HmdfStation *station) {
  assert(index >= 0 && index < this->m_station.size());
  this->m_station[index] = station;
}

void Hmdf::addStation(HmdfStation *station) {
  this->m_station.push_back(station);
}

bool Hmdf::success() const { return this->m_success; }

void Hmdf::setSuccess(bool success) { this->m_success = success; }

int Hmdf::readImeds(QString filename) {
  std::fstream fid(filename.toStdString().c_str());
  if (fid.bad()) return -1;

  //...Read Header
  std::string templine;
  std::getline(fid, templine);
  this->m_header1 =
      QString::fromStdString(StringUtil::sanitizeString(templine));
  std::getline(fid, templine);
  this->m_header2 =
      QString::fromStdString(StringUtil::sanitizeString(templine));
  std::getline(fid, templine);
  this->m_header3 =
      QString::fromStdString(StringUtil::sanitizeString(templine));

  HmdfStation *station;

  //...Read Body
  std::getline(fid, templine);

  while (!fid.eof()) {
    station = new HmdfStation(this);

    templine = StringUtil::sanitizeString(templine);

    QStringList templist =
        QString::fromStdString(templine).split(" ", QString::SkipEmptyParts);

    station->setName(templist.at(0));
    station->setLongitude(templist.at(2).toDouble());
    station->setLatitude(templist.at(1).toDouble());

    while (true) {
      std::getline(fid, templine);

      int year, month, day, hour, minute, second;
      double value;

      bool status = HmdfAsciiParser::splitStringHmdfFormat(
          templine, year, month, day, hour, minute, second, value);

      if (status) {
        qint64 secs =
            QDateTime(QDate(year, month, day), QTime(hour, minute, second))
                .toMSecsSinceEpoch();

        //...Append to the station data
        station->setNext(secs, value);
      } else {
        break;
      }
    }

    //...Add the station
    this->addStation(station);
  }

  return 0;
}

int Hmdf::readNetcdf(QString filename) {
  NetcdfTimeseries *ncts = new NetcdfTimeseries(this);
  ncts->setFilename(filename);
  int ierr = ncts->read();
  if (ierr != 0) {
    delete ncts;
    return 1;
  }
  ierr = ncts->toHmdf(this);
  delete ncts;

  if (ierr != 0) return 1;

  return 0;
}

int Hmdf::writeCsv(QString filename) {
  int i, s;
  QString value;
  QFile output(filename);

  if (!output.open(QIODevice::WriteOnly)) return -1;

  for (s = 0; s < this->nstations(); s++) {
    output.write(
        QString("Station: " + this->station(s)->name() + "\n").toUtf8());
    output.write(QString("Datum: " + this->datum() + "\n").toUtf8());
    output.write(QString("Units: " + this->units() + "\n").toUtf8());
    output.write(QString("\n").toUtf8());
    for (i = 0; i < this->station(s)->numSnaps(); i++) {
      if (QDateTime::fromMSecsSinceEpoch(this->station(s)->date(i)).isValid()) {
        value.sprintf("%10.4e", this->station(s)->data(i));
        output.write(
            QString(QDateTime::fromMSecsSinceEpoch(this->station(s)->date(i))
                        .toString("MM/dd/yyyy,hh:mm,") +
                    value + "\n")
                .toUtf8());
      }
    }
    output.write(QString("\n\n\n").toUtf8());
  }
  output.close();
  return 0;
}

int Hmdf::writeImeds(QString filename) {
  QString value;
  QFile outputFile(filename);

  if (!outputFile.open(QIODevice::WriteOnly)) return -1;

  outputFile.write(QString("% IMEDS generic format - Water Level\n").toUtf8());
  outputFile.write(
      QString("% year month day hour min sec watlev(" + this->units() + ")\n")
          .toUtf8());
  outputFile.write(
      QString("MetOceanViewer    UTC    " + this->datum() + "\n").toUtf8());

  for (int s = 0; s < this->nstations(); s++) {
    outputFile.write(
        QString(this->station(s)->name() + "   " +
                QString::number(this->station(s)->latitude()) +
                "   " +
                QString::number(this->station(s)->longitude()) +
                "\n")
            .toUtf8());

    for (int i = 0; i < this->station(s)->numSnaps(); i++) {
      if (QDateTime::fromMSecsSinceEpoch(this->station(s)->date(i)).isValid()) {
        value.sprintf("%10.4e", this->station(s)->data(i));
        outputFile.write(
            QString(QDateTime::fromMSecsSinceEpoch(this->station(s)->date(i))
                        .toString("yyyy    MM    dd    hh    mm    ss") +
                    "    " + value + "\n")
                .toUtf8());
      }
    }
  }
  outputFile.close();
  return 0;
}

int Hmdf::writeNetcdf(QString filename) { return 0; }
