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
#ifndef OPTIONS_H
#define OPTIONS_H

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QObject>
#include "metoceandata.h"

class Options : public QObject {
  Q_OBJECT
 public:
  explicit Options(QObject *parent = nullptr);

  struct CommandLineOptions {
    int product;
    int datum;
    MetOceanData::serviceTypes service;
    QDateTime startDate;
    QDateTime endDate;
    QString outputFile;
    QStringList station;
  };

  void processOptions();

  CommandLineOptions getCommandLineOptions();

  QCommandLineParser *parser();

 private:
  void addOptions();

  void printStationList(QStringList station, MetOceanData::serviceTypes markerType);
  void readStationList(QStringList &station, MetOceanData::serviceTypes markerType);

  QDateTime checkDateString(QString str);
  MetOceanData::serviceTypes checkServiceString(QString str);
  int checkIntegerString(QString str);
  QCommandLineParser m_parser;
  QCoreApplication *m_application;
};

#endif  // OPTIONS_H
