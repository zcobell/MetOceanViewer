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
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QTimer>
#include <iostream>
#include "driver.h"
#include "version.h"

QDateTime checkDateString(QString str);
int checkServiceString(QString str);
int checkIntegerString(QString str);
void processCommandLineOptions(QCoreApplication &a,
                               Driver::serviceTypes &service, QString &station,
                               int &product, int &datum, QDateTime &startDate,
                               QDateTime &endDate, QString &outputFile);

int main(int argc, char *argv[]) {
  QCoreApplication a(argc, argv);
  QCoreApplication::setApplicationName("MetOceanData");
  QCoreApplication::setApplicationVersion(
      QString::fromStdString(metoceanVersion()));

  Q_INIT_RESOURCE(resource_files);

  int product, datum;
  Driver::serviceTypes service;
  QDateTime startDate, endDate;
  QString outputFile, station;

  processCommandLineOptions(a, service, station, product, datum, startDate,
                            endDate, outputFile);

  Driver *d = new Driver(service, station, product, datum, startDate, endDate,
                         outputFile, &a);
  d->setLoggingActive();
  QObject::connect(d, SIGNAL(finished()), &a, SLOT(quit()));
  QTimer::singleShot(0, d, SLOT(run()));
  return a.exec();
}

void processCommandLineOptions(QCoreApplication &app,
                               Driver::serviceTypes &service, QString &station,
                               int &product, int &datum, QDateTime &startDate,
                               QDateTime &endDate, QString &outputFile) {
  QCommandLineParser parser;
  parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
  parser.setApplicationDescription(
      "Command line interface to MetOceanViewer's library to generate live "
      "time series data.");

  QCommandLineOption cmd_serviceType =
      QCommandLineOption(QStringList() << "s"
                                       << "service",
                         "Service to use to generate data. Can be one of NOAA, "
                         "USGS, NDBC, or XTIDE",
                         "source");
  QCommandLineOption cmd_stationId = QCommandLineOption(
      QStringList() << "n"
                    << "station",
      "Station ID to use to generate data. Ex. NOAA Station 2695540 would "
      "be be specified with '2695540'. XTide stations should be specified "
      "using their ID from MetOceanViewer",
      "id");

  QCommandLineOption cmd_startDate =
      QCommandLineOption(QStringList() << "b"
                                       << "startdate",
                         "Start date for generating the data. Should be "
                         "formatted as: yyyyMMddhhmmss",
                         "date");

  QCommandLineOption cmd_endDate = QCommandLineOption(
      QStringList() << "e"
                    << "enddate",
      "End date for generating the data. Should be formatted as yyyyMMddhhmmss",
      "date");

  QCommandLineOption cmd_product =
      QCommandLineOption(QStringList() << "p"
                                       << "product",
                         "Product index to download numbered from 1 to the "
                         "number of available products. If left unspecified, "
                         "you will be presented with a list of options",
                         "index");

  QCommandLineOption cmd_datum = QCommandLineOption(
      QStringList() << "d"
                    << "datum",
      "Specified datum to use. Only available for NOAA "
      "products. Options "
      "are: \n    (1)  MHHW\n    (2)  MHW\n    (3)  MTL\n    (4)  MSL\n    (5) "
      " MLW\n    (6)  MLLW\n    (7)  NAVD\n    (8)  LWI\n    (9)  HWI\n    "
      "(10) IGLD\n    (11) Station Datum",
      "option");

  QCommandLineOption cmd_outputFile =
      QCommandLineOption(QStringList() << "o"
                                       << "output",
                         "Name of the output file. Format will be guessed from "
                         "extension (.imeds or .nc)",
                         "filename");

  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOptions(QList<QCommandLineOption>()
                    << cmd_serviceType << cmd_stationId << cmd_startDate
                    << cmd_endDate << cmd_product << cmd_outputFile
                    << cmd_datum);

  parser.process(app);

  if (app.arguments().length() == 1) {
    std::cerr << "Error: No command line arguments detected." << std::endl;
    std::cerr.flush();
    parser.showHelp(1);
  }

  if (!parser.isSet(cmd_serviceType)) {
    std::cerr << "Error: No service selected." << std::endl;
    parser.showHelp(1);
  }

  if (!parser.isSet(cmd_stationId)) {
    std::cerr << "Error: No station selected." << std::endl;
    parser.showHelp(1);
  }

  if (!parser.isSet(cmd_startDate)) {
    std::cerr << "Error: No start date selected." << std::endl;
    parser.showHelp(1);
  }

  if (!parser.isSet(cmd_endDate)) {
    std::cerr << "Error: No end date selected." << std::endl;
    parser.showHelp(1);
  }

  if (!parser.isSet(cmd_outputFile)) {
    std::cerr << "Error: No output file specified." << std::endl;
    parser.showHelp(1);
  }

  QString serviceString = parser.value(cmd_serviceType);
  QString startDateString = parser.value(cmd_startDate);
  QString endDateString = parser.value(cmd_endDate);
  station = parser.value(cmd_stationId);
  outputFile = parser.value(cmd_outputFile);

  if (parser.isSet(cmd_product)) {
    QString productString = parser.value(cmd_product);
    product = checkIntegerString(productString);
    if (product == -1) {
      std::cout << "Error: Invalid product selection." << std::endl;
      parser.showHelp(1);
    }
  } else {
    product = -1;
  }

  if (parser.isSet(cmd_datum)) {
    QString datumString = parser.value(cmd_datum);
    datum = checkIntegerString(datumString);
    if (datum == -1) {
      std::cout << "Error: Invalid datum selection." << std::endl;
      parser.showHelp(1);
    }
  } else {
    datum = -1;
  }

  service =
      static_cast<Driver::serviceTypes>(checkServiceString(serviceString));
  if (service == Driver::UNKNOWNSERVICE) {
    std::cerr << "Error: Unknown service specified." << std::endl;
    parser.showHelp(1);
  }

  startDate = checkDateString(startDateString);
  if (startDate.isNull()) {
    std::cerr << "Error: Invalid start date." << std::endl;
    parser.showHelp(1);
  }

  endDate = checkDateString(endDateString);
  if (endDate.isNull()) {
    std::cerr << "Error: Invalid end date." << std::endl;
    parser.showHelp(1);
  }

  outputFile = parser.value(cmd_outputFile);

  if (startDate >= endDate) {
    std::cerr << "Error: Date range is not logical." << std::endl;
    parser.showHelp(1);
  }

  return;
}

int checkServiceString(QString str) {
  str = str.toUpper();
  if (str == "NOAA") return Driver::NOAA;
  if (str == "USGS") return Driver::USGS;
  if (str == "XTIDE") return Driver::XTIDE;
  if (str == "NDBC") return Driver::NDBC;
  return Driver::UNKNOWNSERVICE;
}

QDateTime checkDateString(QString str) {
  QDateTime d = QDateTime();
  d = QDateTime::fromString(str.simplified(), "yyyyMMddhhmmss");
  if (d.isValid()) return d;
  return QDateTime();
}

int checkIntegerString(QString str) {
  bool ok;
  int s = str.toInt(&ok);
  if (!ok && s > 0) {
    return -1;
  } else {
    return s;
  }
}
